/************************************************************************************
   Copyright (C) 2020 MariaDB Corporation AB

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not see <http://www.gnu.org/licenses>
   or write to the Free Software Foundation, Inc.,
   51 Franklin St., Fifth Floor, Boston, MA 02110, USA
*************************************************************************************/


#include <vector>
#include <array>
#include <sstream>

#include "SelectResultSetCapi.h"
#include "Results.h"

#include "MariaDbResultSetMetaData.h"

#include "Protocol.h"
#include "ColumnDefinitionCapi.h"
#include "ExceptionFactory.h"
#include "SqlStates.h"
//#include "com/Packet.h"
#include "com/RowProtocol.h"
#include "protocol/capi/BinRowProtocolCapi.h"
#include "protocol/capi/TextRowProtocolCapi.h"
#include "util/ServerPrepareResult.h"

namespace sql
{
namespace mariadb
{
namespace capi
{
  /**
    * Create Streaming resultSet.
    *
    * @param results results
    * @param protocol current protocol
    * @param spr ServerPrepareResult
    * @param callableResult is it from a callableStatement ?
    * @param eofDeprecated is EOF deprecated
    */
  SelectResultSetCapi::SelectResultSetCapi(Results* results,
                                           Protocol* protocol,
                                           ServerPrepareResult* spr,
                                           bool callableResult,
                                           bool eofDeprecated)
    : statement(results->getStatement()),
      isClosedFlag(false),
      protocol(protocol),
      options(protocol->getOptions()),
      noBackslashEscapes(protocol->noBackslashEscapes()),
      columnsInformation(spr->getColumns()),
      columnNameMap(new ColumnNameMap(columnsInformation)),
      columnInformationLength(columnsInformation.size()),
      fetchSize(results->getFetchSize()),
      dataSize(0),
      resultSetScrollType(results->getResultSetScrollType()),
      dataFetchTime(0),
      rowPointer(-1),
      callableResult(callableResult),
      eofDeprecated(eofDeprecated),
      isEof(false),
      capiConnHandle(NULL),
      capiStmtHandle(spr->getStatementId()),
      timeZone(nullptr)
    //      timeZone(protocol->getTimeZone(),
  {
    row.reset(new capi::BinRowProtocolCapi(columnsInformation, columnInformationLength, results->getMaxFieldSize(), options, capiStmtHandle));

    if (fetchSize == 0 || callableResult) {
      data.reserve(10);//= new char[10]; // This has to be array of arrays. Need to decide what to use for its representation
      mysql_stmt_store_result(capiStmtHandle);
      dataSize= static_cast<std::size_t>(mysql_stmt_num_rows(capiStmtHandle));
      streaming= false;
      resetVariables();
    }
    else {
      lock= protocol->getLock();

      Shared::Results shRes(results);
      protocol->setActiveStreamingResult(shRes);

      protocol->removeHasMoreResults();
      data.reserve(std::max(10, fetchSize)); // Same
      nextStreamingValue();
      streaming= true;
    }
  }

  SelectResultSetCapi::SelectResultSetCapi(Results * results,
                                           Protocol * _protocol,
                                           MYSQL* capiConnHandle,
                                           bool eofDeprecated)
    : statement(results->getStatement()),
      isClosedFlag(false),
      protocol(_protocol),
      options(_protocol->getOptions()),
      noBackslashEscapes(_protocol->noBackslashEscapes()),
      fetchSize(results->getFetchSize()),
      dataSize(0),
      resultSetScrollType(results->getResultSetScrollType()),
      dataFetchTime(0),
      rowPointer(-1),
      callableResult(false),
      eofDeprecated(eofDeprecated),
      isEof(false),
      capiConnHandle(capiConnHandle),
      capiStmtHandle(NULL),
      timeZone(nullptr)
  {
    MYSQL_RES* textNativeResults= NULL;
    if (fetchSize == 0 || callableResult) {
      data.reserve(10);//= new char[10]; // This has to be array of arrays. Need to decide what to use for its representation
      textNativeResults= mysql_store_result(capiConnHandle);
      dataSize= static_cast<size_t>(textNativeResults != NULL ? mysql_num_rows(textNativeResults) : 0);
      streaming= false;
      resetVariables();
    }
    else {
      //TODO: This may be wrong. We get plain ptr and putting it to smart ptr, which will eventually destrct the object w/out object's owner control
      Shared::Results shRes(results);
      protocol->setActiveStreamingResult(shRes);

      protocol->removeHasMoreResults();
      data.reserve(std::max(10, fetchSize)); // Same
      textNativeResults= mysql_use_result(capiConnHandle);

      streaming= true;
    }
    uint32_t fieldCnt= mysql_field_count(capiConnHandle);

    columnsInformation.reserve(fieldCnt);

    for (size_t i= 0; i < fieldCnt; ++i) {
      columnsInformation.emplace_back(new ColumnDefinitionCapi(mysql_fetch_field(textNativeResults)));
    }
    row.reset(new capi::TextRowProtocolCapi(results->getMaxFieldSize(), options, textNativeResults));

    columnNameMap.reset(new ColumnNameMap(columnsInformation));
    columnInformationLength= columnsInformation.size();

    if (streaming) {
      nextStreamingValue();
    }
  }

  /**
    * Create filled result-set.
    *
    * @param columnInformation column information
    * @param resultSet result-set data
    * @param protocol current protocol
    * @param resultSetScrollType one of the following <code>ResultSet</code> constants: <code>
    *     ResultSet.TYPE_FORWARD_ONLY</code>, <code>ResultSet.TYPE_SCROLL_INSENSITIVE</code>, or
    *     <code>ResultSet.TYPE_SCROLL_SENSITIVE</code>
    */
  SelectResultSetCapi::SelectResultSetCapi(
    std::vector<Shared::ColumnDefinition>& columnInformation,
    std::vector<std::vector<sql::bytes>>& resultSet,
    Protocol* protocol,
    int32_t resultSetScrollType)
    : statement(NULL),
      row(new capi::TextRowProtocolCapi(0, this->options, nullptr)),
      data(std::move(resultSet)),
      dataSize(data.size()),
      isClosedFlag(false),
      columnsInformation(columnInformation),
      columnNameMap(new ColumnNameMap(columnsInformation)),
      columnInformationLength(columnInformation.size()),
      isEof(true),
      fetchSize(0),
      resultSetScrollType(resultSetScrollType),
      dataFetchTime(0),
      rowPointer(-1),
      callableResult(false),
      streaming(false),
      capiConnHandle(NULL),
      capiStmtHandle(NULL),
      timeZone(nullptr)
  {
    if (protocol) {
      this->options= protocol->getOptions();
      this->timeZone= protocol->getTimeZone();
    }
    else {
      // this->timeZone= TimeZone.getDefault();
    }
  }

  /**
    * Indicate if result-set is still streaming results from server.
    *
    * @return true if streaming is finished
    */
  bool SelectResultSetCapi::isFullyLoaded() const {

    return isEof;
  }

  void SelectResultSetCapi::fetchAllResults()
  {
    dataSize= 0;
    while (readNextValue()) {
    }
    dataFetchTime++;
  }

  const char * SelectResultSetCapi::getErrMessage()
  {
    if (capiStmtHandle != NULL)
    {
      return mysql_stmt_error(capiStmtHandle);
    }
    else if (capiConnHandle != NULL)
    {
      return mysql_error(capiConnHandle);
    }
    return "";
  }


  const char * SelectResultSetCapi::getSqlState()
  {
    if (capiStmtHandle != NULL)
    {
      return mysql_stmt_error(capiStmtHandle);
    }
    else if (capiConnHandle != NULL)
    {
      return mysql_error(capiConnHandle);
    }
    return "HY000";
  }

  uint32_t SelectResultSetCapi::getErrNo()
  {
    if (capiStmtHandle != NULL)
    {
      return mysql_stmt_errno(capiStmtHandle);
    }
    else if (capiConnHandle != NULL)
    {
      return mysql_errno(capiConnHandle);
    }
    return 0;
  }

  uint32_t SelectResultSetCapi::warningCount()
  {
    if (capiStmtHandle != NULL)
    {
      return mysql_stmt_warning_count(capiStmtHandle);
    }
    else if (capiConnHandle != NULL)
    {
      return mysql_warning_count(capiConnHandle);
    }
    return 0;
  }

  /**
    * When protocol has a current Streaming result (this) fetch all to permit another query is
    * executing.
    *
    * @throws SQLException if any error occur
    */
  void SelectResultSetCapi::fetchRemaining() {
    if (!isEof) {
      std::lock_guard<std::mutex> localScopeLock(*lock);
      try {
        lastRowPointer= -1;
        while (!isEof) {
          addStreamingValue();
        }

      }
      catch (SQLException& queryException) {
        throw *ExceptionFactory::INSTANCE.create(queryException);
      }
      catch (std::exception& ioe) {
        throw handleIoException(ioe);
      }
      dataFetchTime++;
    }
  }

  SQLException SelectResultSetCapi::handleIoException(std::exception& ioe)
  {

    return *ExceptionFactory::INSTANCE.create(
        "Server has closed the connection. \n"
        "Please check net_read_timeout/net_write_timeout/wait_timeout server variables. "
        "If result set contain huge amount of data, Server expects client to"
        " read off the result set relatively fast. "
        "In this case, please consider increasing net_read_timeout session variable"
        " / processing your result set faster (check Streaming result sets documentation for more information)",
        CONNECTION_EXCEPTION.getSqlState(), &ioe);
  }

  /**
    * This permit to replace current stream results by next ones.
    *
    * @throws IOException if socket exception occur
    * @throws SQLException if server return an unexpected error
    */
  void SelectResultSetCapi::nextStreamingValue() {
    lastRowPointer= -1;

    if (resultSetScrollType == TYPE_FORWARD_ONLY) {
      dataSize= 0;
    }

    addStreamingValue();
  }

  /**
    * This permit to add next streaming values to existing resultSet.
    *
    * @throws IOException if socket exception occur
    * @throws SQLException if server return an unexpected error
    */
  void SelectResultSetCapi::addStreamingValue() {

    int32_t fetchSizeTmp= fetchSize;
    while (fetchSizeTmp > 0 && readNextValue()) {
      fetchSizeTmp--;
    }
    dataFetchTime++;
  }


  /**
    * Read next value.
    *
    * @return true if have a new value
    * @throws IOException exception
    * @throws SQLException exception
    */
  bool SelectResultSetCapi::readNextValue()
  {
    switch (row->fetchNext()) {

    case MYSQL_DATA_TRUNCATED: {
      /*protocol->removeActiveStreamingResult();
      protocol->removeHasMoreResults();*/
      protocol->setHasWarnings(true);
      break;

      /*resetVariables();
      throw *ExceptionFactory::INSTANCE.create(
        getErrMessage(),
        getSqlState(),
        getErrNo(),
        NULL,
        false);*/
    }

    case MYSQL_NO_DATA: {
      uint32_t serverStatus;
      uint32_t warnings;

      if (!eofDeprecated) {

        protocol->readEofPacket();
        warnings= warningCount();
        serverStatus= protocol->getServerStatus();

        // CallableResult has been read from intermediate EOF server_status
        // and is mandatory because :
        //
        // - Call query will have an callable resultSet for OUT parameters
        //   this resultSet must be identified and not listed in JDBC statement.getResultSet()
        //
        // - after a callable resultSet, a OK packet is send,
        //   but mysql before 5.7.4 doesn't send MORE_RESULTS_EXISTS flag
        if (callableResult) {
          serverStatus|= MORE_RESULTS_EXISTS;
        }
      }
      else {
        // OK_Packet with a 0xFE header
        // protocol->readOkPacket()?
        serverStatus= protocol->getServerStatus();
        warnings= warningCount();;
        callableResult= (serverStatus & PS_OUT_PARAMETERS)!=0;
      }
      protocol->setServerStatus(serverStatus);
      protocol->setHasWarnings(warnings > 0);

      if ((serverStatus & MORE_RESULTS_EXISTS) == 0) {
        protocol->removeActiveStreamingResult();
      }

      resetVariables();
      return false;
    }
    }


    if (dataSize + 1 >= data.size()) {
      growDataArray();
    }
    //data[dataSize++]= ?;
    return true;
  }

  /**
    * Get current row's raw bytes.
    *
    * @return row's raw bytes
    */
  std::vector<sql::bytes>& SelectResultSetCapi::getCurrentRowData() {
    return data[rowPointer];
  }

  /**
    * Update row's raw bytes. in case of row update, refresh the data. (format must correspond to
    * current resultset binary/text row encryption)
    *
    * @param rawData new row's raw data.
    */
  void SelectResultSetCapi::updateRowData(std::vector<sql::bytes>& rawData)
  {
    data[rowPointer]= rawData;
    row->resetRow(data[rowPointer]);
  }

  /**
    * Delete current data. Position cursor to the previous row->
    *
    * @throws SQLException if previous() fail.
    */
  void SelectResultSetCapi::deleteCurrentRowData() {

    data.erase(data.begin()+lastRowPointer);
    dataSize--;
    lastRowPointer= -1;
    previous();
  }

  void SelectResultSetCapi::addRowData(std::vector<sql::bytes>& rawData) {
    if (dataSize +1 >= data.size()) {
      growDataArray();
    }
    data[dataSize]= rawData;
    rowPointer= dataSize;
    dataSize++;
  }

  /*int32_t SelectResultSetCapi::skipLengthEncodedValue(std::string& buf, int32_t pos) {
    int32_t type= buf[pos++] &0xff;
    switch (type) {
    case 251:
      return pos;
    case 252:
      return pos +2 +(0xffff &(((buf[pos] &0xff)+((buf[pos +1] &0xff)<<8))));
    case 253:
      return pos
        +3
        +(0xffffff
          &((buf[pos] &0xff)
            +((buf[pos +1] &0xff)<<8)
            +((buf[pos +2] &0xff)<<16)));
    case 254:
      return (int32_t)
        (pos
          +8
          +((buf[pos] &0xff)
            +((int64_t)(buf[pos +1] &0xff)<<8)
            +((int64_t)(buf[pos +2] &0xff)<<16)
            +((int64_t)(buf[pos +3] &0xff)<<24)
            +((int64_t)(buf[pos +4] &0xff)<<32)
            +((int64_t)(buf[pos +5] &0xff)<<40)
            +((int64_t)(buf[pos +6] &0xff)<<48)
            +((int64_t)(buf[pos +7] &0xff)<<56)));
    default:
      return pos +type;
    }
  }*/

  /** Grow data array. */
  void SelectResultSetCapi::growDataArray() {
    int32_t newCapacity= data.size() + (data.size() >>1);

    if (newCapacity - MAX_ARRAY_SIZE > 0) {
      newCapacity= MAX_ARRAY_SIZE;
    }
    // Commenting this out so far as we do not put data from server here, thus growing is in vain
    //data.reserve(newCapacity);
  }

  /**
    * Connection.abort() has been called, abort result-set.
    *
    * @throws SQLException exception
    */
  void SelectResultSetCapi::abort() {
    isClosedFlag= true;
    resetVariables();

    for (auto& row : data) {
      row.clear();
    }

    if (statement != nullptr) {
      statement->checkCloseOnCompletion(this);
      statement= NULL;
    }
  }

  /** Close resultSet. */
  void SelectResultSetCapi::close() {
    isClosedFlag= true;
    if (!isEof) {
      std::unique_lock<std::mutex> localScopeLock(*lock);
      try {
        while (!isEof) {
          dataSize= 0; // to avoid storing data
          readNextValue();
        }
      }
      catch (SQLException& queryException) {
        throw *ExceptionFactory::INSTANCE.create(queryException);
      }
      catch (std::runtime_error& ioe) {
        resetVariables();
        localScopeLock.unlock();
        throw handleIoException(ioe);
      }
    }
    resetVariables();

    for (size_t i= 0; i < data.size(); i++)
    {
      data[i].clear();

    }

    if (statement != nullptr) {
      statement->checkCloseOnCompletion(this);
      statement= nullptr;
    }
  }

  void SelectResultSetCapi::resetVariables() {
    protocol= nullptr;
    isEof= true;
  }


  void SelectResultSetCapi::fetchNext()
  {
    rowPointer++;
    if (data.size() > 0) {
      row->resetRow(data[rowPointer]);
    }
    else {
      row->fetchNext();
    }
    lastRowPointer= rowPointer;
  }

  bool SelectResultSetCapi::next() {
    if (isClosedFlag) {
      throw SQLException("Operation not permit on a closed resultSet", "HY000");
    }
    if (rowPointer < static_cast<int32_t>(dataSize) - 1) {
      fetchNext();
      return true;
    }
    else {
      if (streaming && !isEof) {
        std::lock_guard<std::mutex> localScopeLock(*lock);
        try {
          if (!isEof) {
            nextStreamingValue();
          }
        }
        catch (std::exception& ioe) {
          throw handleIoException(ioe);
        }

        if (resultSetScrollType == TYPE_FORWARD_ONLY) {

          rowPointer= 0;
          return dataSize >0;
        }
        else {
          rowPointer++;
          return dataSize > static_cast<uint32_t>(rowPointer);
        }
      }


      rowPointer= dataSize;
      return false;
    }
  }

  void SelectResultSetCapi::resetRow()
  {
    if (data.size() > 0) {
      row->resetRow(data[rowPointer]);
    }
    else {
      row->fetchAtPosition(rowPointer);
    }
    lastRowPointer= rowPointer;
  }

  void SelectResultSetCapi::checkObjectRange(int32_t position) {
    if (rowPointer < 0) {
      throw SQLDataException("Current position is before the first row", "22023");
    }

    if (static_cast<uint32_t>(rowPointer) >= dataSize) {
      throw SQLDataException("Current position is after the last row", "22023");
    }

    if (position <= 0 || position > columnInformationLength) {
      throw IllegalArgumentException("No such column: "+position, "22023");
    }

    if (lastRowPointer != rowPointer) {
      resetRow();
    }
    row->setPosition(position - 1);
  }

  SQLWarning* SelectResultSetCapi::getWarnings() {
    if (this->statement == NULL) {
      return NULL;
    }
    return this->statement->getWarnings();
  }

  void SelectResultSetCapi::clearWarnings() {
    if (this->statement != nullptr) {
      this->statement->clearWarnings();
    }
  }

  bool SelectResultSetCapi::isBeforeFirst() {
    checkClose();
    return (dataFetchTime >0) ? rowPointer == -1 && dataSize > 0 : rowPointer == -1;
  }

  bool SelectResultSetCapi::isAfterLast() {
    checkClose();
    if (static_cast<uint32_t>(rowPointer) < dataSize) {

      return false;
    }
    else {

      if (streaming && !isEof) {

        std::lock_guard<std::mutex> localScopeLock(*lock);
        try {

          if (!isEof) {
            addStreamingValue();
          }
        }
        catch (std::exception& ioe) {
          throw handleIoException(ioe);
        }

        return dataSize == rowPointer;
      }

      return dataSize >0 ||dataFetchTime >1;
    }
  }

  bool SelectResultSetCapi::isFirst() {
    checkClose();
    return dataFetchTime == 1 &&rowPointer == 0 &&dataSize >0;
  }

  bool SelectResultSetCapi::isLast() {
    checkClose();
    if (static_cast<uint32_t>(rowPointer) < dataSize -1) {
      return false;
    }
    else if (isEof) {
      return rowPointer == dataSize -1 && dataSize >0;
    }
    else {


      std::lock_guard<std::mutex> localScopeLock(*lock);
      try {
        if (!isEof) {
          addStreamingValue();
        }
      }
      catch (std::exception& ioe) {
        throw handleIoException(ioe);
      }

      if (isEof) {

        return rowPointer == dataSize -1 &&dataSize >0;
      }

      return false;
    }
  }

  void SelectResultSetCapi::beforeFirst() {
    checkClose();

    if (streaming &&resultSetScrollType == TYPE_FORWARD_ONLY) {
      throw SQLException("Invalid operation for result set type TYPE_FORWARD_ONLY");
    }
    rowPointer= -1;
  }

  void SelectResultSetCapi::afterLast() {
    checkClose();
    fetchRemaining();
    rowPointer= dataSize;
  }

  bool SelectResultSetCapi::first() {
    checkClose();

    if (streaming &&resultSetScrollType == TYPE_FORWARD_ONLY) {
      throw SQLException("Invalid operation for result set type TYPE_FORWARD_ONLY");
    }

    rowPointer= 0;
    return dataSize >0;
  }

  bool SelectResultSetCapi::last() {
    checkClose();
    fetchRemaining();
    rowPointer= dataSize - 1;
    return dataSize > 0;
  }

  int32_t SelectResultSetCapi::getRow() {
    checkClose();
    if (streaming &&resultSetScrollType == TYPE_FORWARD_ONLY) {
      return 0;
    }
    return rowPointer +1;
  }

  bool SelectResultSetCapi::absolute(int32_t row) {
    checkClose();

    if (streaming &&resultSetScrollType == TYPE_FORWARD_ONLY) {
      throw SQLException("Invalid operation for result set type TYPE_FORWARD_ONLY");
    }

    if (static_cast<uint32_t>(row) >=0 && static_cast<uint32_t>(row) <= dataSize) {
      rowPointer= row - 1;
      return true;
    }


    fetchRemaining();

    if (row >= 0) {

      if (static_cast<uint32_t>(row) <= dataSize) {
        rowPointer= row -1;
        return true;
      }

      rowPointer= dataSize;
      return false;

    }
    else {

      if (dataSize + row >=0) {

        rowPointer= dataSize +row;
        return true;
      }

      rowPointer= -1;
      return false;
    }
  }

  bool SelectResultSetCapi::relative(int32_t rows) {
    checkClose();
    if (streaming &&resultSetScrollType == TYPE_FORWARD_ONLY) {
      throw SQLException("Invalid operation for result set type TYPE_FORWARD_ONLY");
    }
    int32_t newPos= rowPointer +rows;
    if (newPos <=-1) {
      rowPointer= -1;
      return false;
    }
    else if (static_cast<uint32_t>(newPos) >=dataSize) {
      rowPointer= dataSize;
      return false;
    }
    else {
      rowPointer= newPos;
      return true;
    }
  }

  bool SelectResultSetCapi::previous() {
    checkClose();
    if (streaming && resultSetScrollType == TYPE_FORWARD_ONLY) {
      throw SQLException("Invalid operation for result set type TYPE_FORWARD_ONLY");
    }
    if (rowPointer >-1) {
      rowPointer--;
      return rowPointer != -1;
    }
    return false;
  }

  int32_t SelectResultSetCapi::getFetchDirection() {
    return FETCH_UNKNOWN;
  }

  void SelectResultSetCapi::setFetchDirection(int32_t direction) {
    if (direction == FETCH_REVERSE) {
      throw SQLException(
        "Invalid operation. Allowed direction are ResultSet::FETCH_FORWARD and ResultSet::FETCH_UNKNOWN");
    }
  }

  int32_t SelectResultSetCapi::getFetchSize() {
    return this->fetchSize;
  }

  void SelectResultSetCapi::setFetchSize(int32_t fetchSize) {
    if (streaming &&fetchSize == 0) {
      std::lock_guard<std::mutex> localScopeLock(*lock);
      try {

        while (!isEof) {
          addStreamingValue();
        }
      }
      catch (std::exception& ioe) {
        throw handleIoException(ioe);
      }
      streaming= dataFetchTime == 1;
    }
    this->fetchSize= fetchSize;
  }

  int32_t SelectResultSetCapi::getType() {
    return resultSetScrollType;
  }

  int32_t SelectResultSetCapi::getConcurrency() {
    return CONCUR_READ_ONLY;
  }

  void SelectResultSetCapi::checkClose() {
    if (isClosedFlag) {
      throw SQLException("Operation not permit on a closed resultSet", "HY000");
    }
  }

  bool SelectResultSetCapi::isCallableResult() {
    return callableResult;
  }

  bool SelectResultSetCapi::isClosed() const {
    return isClosedFlag;
  }

  MariaDbStatement* SelectResultSetCapi::getStatement() {
    return statement;
  }

  void SelectResultSetCapi::setStatement(MariaDbStatement* statement)
  {
    this->statement= statement;
  }

  /** {inheritDoc}. */
  bool SelectResultSetCapi::wasNull() {
    return row->wasNull();
  }

  bool SelectResultSetCapi::isNull(int32_t columnIndex)
  {
    checkObjectRange(columnIndex);
    return row->lastValueWasNull();
  }

  bool SelectResultSetCapi::isNull(const SQLString & columnLabel)
  {
    return isNull(findColumn(columnLabel));
  }

#ifdef MAYBE_IN_BETA
  /** {inheritDoc}. */
  std::istream* SelectResultSetCapi::getAsciiStream(const SQLString& columnLabel) {
    return getAsciiStream(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  std::istream* SelectResultSetCapi::getAsciiStream(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    return new ByteArrayInputStream(
      new SQLString(row->buf, row->pos, row->getLengthMaxFieldSize()).c_str());/*, StandardCharsets.UTF_8*/
  }
#endif

  /** {inheritDoc}. */
  SQLString SelectResultSetCapi::getString(int32_t columnIndex)
  {
    checkObjectRange(columnIndex);
    std::unique_ptr<SQLString> res= row->getInternalString(columnsInformation[columnIndex -1].get());

    if (res) {
      return *res;
    }
    else {
      return emptyStr;
    }
  }

  /** {inheritDoc}. */
  SQLString SelectResultSetCapi::getString(const SQLString& columnLabel) {
    return getString(findColumn(columnLabel));
  }


  SQLString SelectResultSetCapi::zeroFillingIfNeeded(const SQLString& value, ColumnDefinition* columnInformation)
  {
    if (columnInformation->isZeroFill()) {
      SQLString zeroAppendStr;
      int64_t zeroToAdd= columnInformation->getDisplaySize() - value.size();
      while ((zeroToAdd--) > 0) {
        zeroAppendStr.append("0");
      }
      return zeroAppendStr.append(value);
    }
    return value;
  }

  /** {inheritDoc}. */
  std::istream* SelectResultSetCapi::getBinaryStream(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    blobBuffer[columnIndex].reset(new memBuf(row->fieldBuf.arr + row->pos, row->fieldBuf.arr + row->pos + row->getLengthMaxFieldSize()));
    return new std::istream(blobBuffer[columnIndex].get());
  }

  /** {inheritDoc}. */
  std::istream* SelectResultSetCapi::getBinaryStream(const SQLString& columnLabel) {
    return getBinaryStream(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  int32_t SelectResultSetCapi::getInt(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalInt(columnsInformation[columnIndex -1].get());
  }

  /** {inheritDoc}. */  int32_t SelectResultSetCapi::getInt(const SQLString& columnLabel) {
    return getInt(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  int64_t SelectResultSetCapi::getLong(const SQLString& columnLabel) {
    return getLong(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  int64_t SelectResultSetCapi::getLong(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalLong(columnsInformation[columnIndex -1].get());
  }


  uint64_t SelectResultSetCapi::getUInt64(const SQLString & columnLabel)
  {
    return getUInt64(findColumn(columnLabel));
  }


  uint64_t SelectResultSetCapi::getUInt64(int32_t columnIndex)
  {
    checkObjectRange(columnIndex);
    return static_cast<uint64_t>(row->getInternalULong(columnsInformation[columnIndex -1].get()));
  }


  uint32_t SelectResultSetCapi::getUInt(const SQLString& columnLabel)
  {
    return getUInt(findColumn(columnLabel));
  }


  uint32_t SelectResultSetCapi::getUInt(int32_t columnIndex)
  {
    checkObjectRange(columnIndex);
    return static_cast<uint32_t>(row->getInternalULong(columnsInformation[columnIndex - 1].get()));
  }


  /** {inheritDoc}. */
  float SelectResultSetCapi::getFloat(const SQLString& columnLabel) {
    return getFloat(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  float SelectResultSetCapi::getFloat(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalFloat(columnsInformation[columnIndex -1].get());
  }

  /** {inheritDoc}. */
  long double SelectResultSetCapi::getDouble(const SQLString& columnLabel) {
    return getDouble(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  long double SelectResultSetCapi::getDouble(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalDouble(columnsInformation[columnIndex -1].get());
  }

#ifdef JDBC_SPECIFIC_TYPES_IMPLEMENTED
  /** {inheritDoc}. */
  BigDecimal SelectResultSetCapi::getBigDecimal(const SQLString& columnLabel, int32_t scale) {
    return getBigDecimal(findColumn(columnLabel), scale);
  }

  /** {inheritDoc}. */
  BigDecimal SelectResultSetCapi::getBigDecimal(int32_t columnIndex, int32_t scale) {
    checkObjectRange(columnIndex);
    return row->getInternalBigDecimal(columnsInformation[columnIndex -1]);
  }

  /** {inheritDoc}. */
  BigDecimal SelectResultSetCapi::getBigDecimal(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalBigDecimal(columnsInformation[columnIndex -1]);
  }

  /** {inheritDoc}. */
  BigDecimal SelectResultSetCapi::getBigDecimal(const SQLString& columnLabel) {
    return getBigDecimal(findColumn(columnLabel));
  }
#endif
#ifdef MAYBE_IN_BETA
  /** {inheritDoc}. */
  SQLString SelectResultSetCapi::getBytes(const SQLString& columnLabel) {
    return getBytes(findColumn(columnLabel));
  }
  /** {inheritDoc}. */
  SQLString SelectResultSetCapi::getBytes(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    char* data= new char[row->getLengthMaxFieldSize()];
    System.arraycopy(row->buf, row->pos, data, 0, row->getLengthMaxFieldSize());
    return data;
  }


  /** {inheritDoc}. */
  Date* SelectResultSetCapi::getDate(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalDate(columnsInformation[columnIndex -1], NULL, timeZone);
  }

  /** {inheritDoc}. */
  Date* SelectResultSetCapi::getDate(const SQLString& columnLabel) {
    return getDate(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  Time* SelectResultSetCapi::getTime(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalTime(columnsInformation[columnIndex -1], NULL, timeZone);
  }

  /** {inheritDoc}. */  Time SelectResultSetCapi::getTime(const SQLString& columnLabel) {
    return getTime(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  Timestamp* SelectResultSetCapi::getTimestamp(const SQLString& columnLabel) {
    return getTimestamp(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  Timestamp* SelectResultSetCapi::getTimestamp(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalTimestamp(columnsInformation[columnIndex -1], NULL, timeZone);
  }
#endif

#ifdef JDBC_SPECIFIC_TYPES_IMPLEMENTED
  /** {inheritDoc}. */
  Date* SelectResultSetCapi::getDate(int32_t columnIndex, Calendar& cal) {
    checkObjectRange(columnIndex);
    return row->getInternalDate(columnsInformation[columnIndex -1], cal, timeZone);
  }

  /** {inheritDoc}. */
  Date* SelectResultSetCapi::getDate(const SQLString& columnLabel, Calendar& cal) {
    return getDate(findColumn(columnLabel), cal);
  }

  /** {inheritDoc}. */
  Time* SelectResultSetCapi::getTime(int32_t columnIndex, Calendar& cal) {
    checkObjectRange(columnIndex);
    return row->getInternalTime(columnsInformation[columnIndex -1], cal, timeZone);
  }

  /** {inheritDoc}. */
  Time* SelectResultSetCapi::getTime(const SQLString& columnLabel, Calendar& cal) {
    return getTime(findColumn(columnLabel), cal);
  }

  /** {inheritDoc}. */
  Timestamp* SelectResultSetCapi::getTimestamp(int32_t columnIndex, Calendar& cal) {
    checkObjectRange(columnIndex);
    return row->getInternalTimestamp(columnsInformation[columnIndex -1], cal, timeZone);
  }

  /** {inheritDoc}. */
  Timestamp* SelectResultSetCapi::getTimestamp(const SQLString& columnLabel, Calendar& cal) {
    return getTimestamp(findColumn(columnLabel), cal);
  }
#endif

  /** {inheritDoc}. */
  SQLString SelectResultSetCapi::getCursorName() {
    throw ExceptionFactory::INSTANCE.notSupported("Cursors not supported");
  }

  /** {inheritDoc}. */
  sql::ResultSetMetaData* SelectResultSetCapi::getMetaData() {
    return new MariaDbResultSetMetaData(columnsInformation, options, forceAlias);
  }

  /** {inheritDoc}. */
  int32_t SelectResultSetCapi::findColumn(const SQLString& columnLabel) {
    return columnNameMap->getIndex(columnLabel) + 1;
  }

#ifdef JDBC_SPECIFIC_TYPES_IMPLEMENTED

  /** {inheritDoc}. */
  sql::Object* SelectResultSetCapi::getObject(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    return row->getInternalObject(columnsInformation[columnIndex -1], timeZone);
  }

  /** {inheritDoc}. */
  sql::Object* SelectResultSetCapi::getObject(const SQLString& columnLabel) {
    return getObject(findColumn(columnLabel));
  }

  template <class T>T getObject(int32_t columnIndex, Classtemplate <class T>type) {
    if (type.empty() == true) {
      throw SQLException("Class type cannot be NULL");
    }
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    ColumnDefinition col= columnsInformation[columnIndex -1];

    if ((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalString(col, NULL, timeZone);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)static_cast<int32_t>(row->getInternalInt(col));

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)static_cast<int64_t>(row->getInternalLong(col));

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)(Short)row->getInternalShort(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)(Double)row->getInternalDouble(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)(Float)row->getInternalFloat(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)(Byte)row->getInternalByte(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      char* data= new char[row->getLengthMaxFieldSize()];
      System.arraycopy(row->buf, row->pos, data, 0, row->getLengthMaxFieldSize());
      return (T)data;

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalDate(col, NULL, timeZone);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalTime(col, NULL, timeZone);

    }
    else if ((type.compare(SQLString.class) == 0)||((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalTimestamp(col, NULL, timeZone);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)(Boolean)row->getInternalBoolean(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      calendar=  .getInstance(timeZone);
      Timestamp timestamp= row->getInternalTimestamp(col, NULL, timeZone);
      if (timestamp.empty() == true) {
        return NULL;
      }
      calendar.setTimeInMillis(timestamp.getTime());
      return type.cast(calendar);

    }
    else if ((type.compare(SQLString.class) == 0)||((type.compare(SQLString.class) == 0)) {
      return (T)new MariaDbClob(row->buf, row->pos, row->getLengthMaxFieldSize());

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)new ByteArrayInputStream(row->buf, row->pos, row->getLengthMaxFieldSize());

    }
    else if ((type.compare(SQLString.class) == 0)) {
      SQLString value= row->getInternalString(col, NULL, timeZone);
      if (value.empty() == true) {
        return NULL;
      }
      return (T)new StringReader(value);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalBigDecimal(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalBigInteger(col);
    }
    else if ((type.compare(SQLString.class) == 0)) {
      return (T)row->getInternalBigDecimal(col);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      ZonedDateTime zonedDateTime =
        row->getInternalZonedDateTime(col, LocalDateTime.class, timeZone);
      return zonedDateTime.empty() == true
        ? NULL
        : type.cast(zonedDateTime.withZoneSameInstant(ZoneId.systemDefault()).toLocalDateTime());

    }
    else if ((type.compare(SQLString.class) == 0)) {
      ZonedDateTime zonedDateTime =
        row->getInternalZonedDateTime(col, ZonedDateTime.class, timeZone);
      if (zonedDateTime.empty() == true) {
        return NULL;
      }
      return type.cast(row->getInternalZonedDateTime(col, ZonedDateTime.class, timeZone));

    }
    else if ((type.compare(SQLString.class) == 0)) {
      ZonedDateTime tmpZonedDateTime =
        row->getInternalZonedDateTime(col, OffsetDateTime.class, timeZone);
      return tmpZonedDateTime.empty() == true ? NULL : type.cast(tmpZonedDateTime.toOffsetDateTime());

    }
    else if ((type.compare(SQLString.class) == 0)) {
      LocalDate localDate= row->getInternalLocalDate(col, timeZone);
      if (localDate.empty() == true) {
        return NULL;
      }
      return type.cast(localDate);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      LocalDate localDate= row->getInternalLocalDate(col, timeZone);
      if (localDate.empty() == true) {
        return NULL;
      }
      return type.cast(localDate);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      LocalTime localTime= row->getInternalLocalTime(col, timeZone);
      if (localTime.empty() == true) {
        return NULL;
      }
      return type.cast(localTime);

    }
    else if ((type.compare(SQLString.class) == 0)) {
      OffsetTime offsetTime= row->getInternalOffsetTime(col, timeZone);
      if (offsetTime.empty() == true) {
        return NULL;
      }
      return type.cast(offsetTime);
    }
    throw ExceptionFactory::INSTANCE.notSupported(
      "Type class '"+type.getName()+"' is not supported");
  }

  /** {inheritDoc}. */
  std::istringstream* SelectResultSetCapi::getCharacterStream(const SQLString& columnLabel) {
    return getCharacterStream(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  std::istringstream* SelectResultSetCapi::getCharacterStream(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    SQLString value= row->getInternalString(columnsInformation[columnIndex -1], NULL, timeZone);
    if (value.empty() == true) {
      return NULL;
    }
    return new StringReader(value);
  }

  /** {inheritDoc}. */
  std::istringstream* SelectResultSetCapi::getNCharacterStream(int32_t columnIndex) {
    return getCharacterStream(columnIndex);
  }

  /** {inheritDoc}. */
  std::istringstream* SelectResultSetCapi::getNCharacterStream(const SQLString& columnLabel) {
    return getCharacterStream(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  Ref* SelectResultSetCapi::getRef(int32_t columnIndex) {

    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  Ref* SelectResultSetCapi::getRef(const SQLString& columnLabel) {
    throw ExceptionFactory::INSTANCE.notSupported("Getting REFs not supported");
  }

  /** {inheritDoc}. */
  Clob* SelectResultSetCapi::getClob(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    return new MariaDbClob(row->buf, row->pos, row->length);
  }

  /** {inheritDoc}. */
  Clob* SelectResultSetCapi::getClob(const SQLString& columnLabel) {
    return getClob(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  sql::Array* SelectResultSetCapi::getArray(int32_t columnIndex) {
    throw ExceptionFactory::INSTANCE.notSupported("Arrays are not supported");
  }

  /** {inheritDoc}. */
  sql::Array* SelectResultSetCapi::getArray(const SQLString& columnLabel) {
    return getArray(findColumn(columnLabel));
  }


  URL* SelectResultSetCapi::getURL(int32_t columnIndex)
  {
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    try {
      return new URL(row->getInternalString(columnsInformation[columnIndex -1], NULL, timeZone));
    }
    catch (MalformedURLException& e) {
      throw ExceptionMapper::getSqlException("Could not parse as URL");
    }
  }

  URL* SelectResultSetCapi::getURL(const SQLString& columnLabel) {
    return getURL(findColumn(columnLabel));
  }


  /** {inheritDoc}. */
  NClob* SelectResultSetCapi::getNClob(int32_t columnIndex) {
    checkObjectRange(columnIndex);
    if (row->lastValueWasNull()) {
      return NULL;
    }
    return new MariaDbClob(row->buf, row->pos, row->length);
  }

  /** {inheritDoc}. */
  NClob* SelectResultSetCapi::getNClob(const SQLString& columnLabel) {
    return getNClob(findColumn(columnLabel));
  }

  SQLXML* SelectResultSetCapi::getSQLXML(int32_t columnIndex) {
    throw ExceptionFactory::INSTANCE.notSupported("SQLXML not supported");
  }

  SQLXML* SelectResultSetCapi::getSQLXML(const SQLString& columnLabel) {
    throw ExceptionFactory::INSTANCE.notSupported("SQLXML not supported");
  }

  /** {inheritDoc}. */  SQLString SelectResultSetCapi::getNString(int32_t columnIndex) {
    return getString(columnIndex);
  }

  /** {inheritDoc}. */
  SQLString SelectResultSetCapi::getNString(const SQLString& columnLabel) {
    return getString(findColumn(columnLabel));
  }
#endif

  /** {inheritDoc}. */
  Blob* SelectResultSetCapi::getBlob(int32_t columnIndex) {
    return getBinaryStream(columnIndex);
  }

  /** {inheritDoc}. */
  Blob* SelectResultSetCapi::getBlob(const SQLString& columnLabel) {
    return getBlob(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  RowId* SelectResultSetCapi::getRowId(int32_t columnIndex) {
    throw ExceptionFactory::INSTANCE.notSupported("RowIDs not supported");
  }

  /** {inheritDoc}. */
  RowId* SelectResultSetCapi::getRowId(const SQLString& columnLabel) {
    throw ExceptionFactory::INSTANCE.notSupported("RowIDs not supported");
  }

  /** {inheritDoc}. */
  bool SelectResultSetCapi::getBoolean(int32_t index) {
    checkObjectRange(index);
    return row->getInternalBoolean(columnsInformation[index -1].get());
  }

  /** {inheritDoc}. */
  bool SelectResultSetCapi::getBoolean(const SQLString& columnLabel) {
    return getBoolean(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  int8_t SelectResultSetCapi::getByte(int32_t index) {
    checkObjectRange(index);
    return row->getInternalByte(columnsInformation[index -1].get());
  }

  /** {inheritDoc}. */
  int8_t SelectResultSetCapi::getByte(const SQLString& columnLabel) {
    return getByte(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  short SelectResultSetCapi::getShort(int32_t index) {
    checkObjectRange(index);
    return row->getInternalShort(columnsInformation[index -1].get());
  }

  /** {inheritDoc}. */
  short SelectResultSetCapi::getShort(const SQLString& columnLabel) {
    return getShort(findColumn(columnLabel));
  }

  /** {inheritDoc}. */
  bool SelectResultSetCapi::rowUpdated() {
    throw ExceptionFactory::INSTANCE.notSupported(
      "Detecting row updates are not supported");
  }

  /** {inheritDoc}. */
  bool SelectResultSetCapi::rowInserted() {
    throw ExceptionFactory::INSTANCE.notSupported("Detecting inserts are not supported");
  }

  /** {inheritDoc}. */
  bool SelectResultSetCapi::rowDeleted() {
    throw ExceptionFactory::INSTANCE.notSupported("Row deletes are not supported");
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::insertRow() {
    throw ExceptionFactory::INSTANCE.notSupported(
      "insertRow are not supported when using ResultSet::CONCUR_READ_ONLY");
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::deleteRow() {
    throw ExceptionFactory::INSTANCE.notSupported(
      "deleteRow are not supported when using ResultSet::CONCUR_READ_ONLY");
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::refreshRow() {
    throw ExceptionFactory::INSTANCE.notSupported(
      "refreshRow are not supported when using ResultSet::CONCUR_READ_ONLY");
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::moveToInsertRow() {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::moveToCurrentRow() {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::cancelRowUpdates() {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  std::size_t sql::mariadb::capi::SelectResultSetCapi::rowsCount()
  {
    return dataSize;
  }

#ifdef RS_UPDATE_FUNCTIONALITY_IMPLEMENTED
  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNull(int32_t columnIndex) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNull(const SQLString& columnLabel) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateBoolean(int32_t columnIndex, bool _bool) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateBoolean(const SQLString& columnLabel, bool value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateByte(int32_t columnIndex, char value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateByte(const SQLString& columnLabel, char value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateShort(int32_t columnIndex, short value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateShort(const SQLString& columnLabel, short value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateInt(int32_t columnIndex, int32_t value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateInt(const SQLString& columnLabel, int32_t value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateFloat(int32_t columnIndex, float value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateFloat(const SQLString& columnLabel, float value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateDouble(int32_t columnIndex, double value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateDouble(const SQLString& columnLabel, double value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBigDecimal(int32_t columnIndex, BigDecimal value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBigDecimal(const SQLString& columnLabel, BigDecimal value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateString(int32_t columnIndex, const SQLString& value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateString(const SQLString& columnLabel, const SQLString& value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBytes(int32_t columnIndex, std::string& value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBytes(const SQLString& columnLabel, std::string& value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateDate(int32_t columnIndex, Date date) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateDate(const SQLString& columnLabel, Date value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateTime(int32_t columnIndex, Time time) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateTime(const SQLString& columnLabel, Time value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateTimestamp(int32_t columnIndex, Timestamp timeStamp) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateTimestamp(const SQLString& columnLabel, Timestamp value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateAsciiStream(int32_t columnIndex, std::istream* inputStream, int32_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateAsciiStream(const SQLString& columnLabel, std::istream* inputStream) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateAsciiStream(const SQLString& columnLabel, std::istream* value, int32_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateAsciiStream(int32_t columnIndex, std::istream* inputStream, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateAsciiStream(const SQLString& columnLabel, std::istream* inputStream, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateAsciiStream(int32_t columnIndex, std::istream* inputStream) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBinaryStream(int32_t columnIndex, std::istream* inputStream, int32_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBinaryStream(int32_t columnIndex, std::istream* inputStream, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBinaryStream(const SQLString& columnLabel, std::istream* value, int32_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBinaryStream(const SQLString& columnLabel, std::istream* inputStream, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBinaryStream(int32_t columnIndex, std::istream* inputStream) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBinaryStream(const SQLString& columnLabel, std::istream* inputStream) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateCharacterStream(int32_t columnIndex, std::istringstream& value, int32_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateCharacterStream(int32_t columnIndex, std::istringstream& value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateCharacterStream(const SQLString& columnLabel, std::istringstream& reader, int32_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateCharacterStream(int32_t columnIndex, std::istringstream& value, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateCharacterStream(const SQLString& columnLabel, std::istringstream& reader, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateCharacterStream(const SQLString& columnLabel, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateObject(int32_t columnIndex, sql::Object* value, int32_t scaleOrLength) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateObject(int32_t columnIndex, sql::Object* value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateObject(const SQLString& columnLabel, sql::Object* value, int32_t scaleOrLength) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateObject(const SQLString& columnLabel, sql::Object* value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateLong(const SQLString& columnLabel, int64_t value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateLong(int32_t columnIndex, int64_t value) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateRow() {
    throw ExceptionFactory::INSTANCE.notSupported(
      "updateRow are not supported when using ResultSet::CONCUR_READ_ONLY");
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateRef(int32_t columnIndex, Ref& ref) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateRef(const SQLString& columnLabel, Ref& ref) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBlob(int32_t columnIndex, Blob& blob) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBlob(const SQLString& columnLabel, Blob& blob) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBlob(int32_t columnIndex, std::istream* inputStream) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBlob(const SQLString& columnLabel, std::istream* inputStream) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBlob(int32_t columnIndex, std::istream* inputStream, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateBlob(const SQLString& columnLabel, std::istream* inputStream, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateClob(int32_t columnIndex, Clob& clob) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateClob(const SQLString& columnLabel, Clob& clob) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateClob(int32_t columnIndex, std::istringstream& reader, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateClob(const SQLString& columnLabel, std::istringstream& reader, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateClob(int32_t columnIndex, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateClob(const SQLString& columnLabel, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateArray(int32_t columnIndex, sql::Array& array) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateArray(const SQLString& columnLabel, sql::Array& array) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateRowId(int32_t columnIndex, RowId& rowId) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateRowId(const SQLString& columnLabel, RowId& rowId) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateNString(int32_t columnIndex, const SQLString& nstring) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateNString(const SQLString& columnLabel, const SQLString& nstring) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateNClob(int32_t columnIndex, NClob& nclob) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateNClob(const SQLString& columnLabel, NClob& nclob) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNClob(int32_t columnIndex, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNClob(const SQLString& columnLabel, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNClob(int32_t columnIndex, std::istringstream& reader, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */  void SelectResultSetCapi::updateNClob(const SQLString& columnLabel, std::istringstream& reader, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  void SelectResultSetCapi::updateSQLXML(int32_t columnIndex, SQLXML& xmlObject) {
    throw ExceptionFactory::INSTANCE.notSupported("SQLXML not supported");
  }

  void SelectResultSetCapi::updateSQLXML(const SQLString& columnLabel, SQLXML& xmlObject) {
    throw ExceptionFactory::INSTANCE.notSupported("SQLXML not supported");
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNCharacterStream(int32_t columnIndex, std::istringstream& value, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNCharacterStream(const SQLString& columnLabel, std::istringstream& reader, int64_t length) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNCharacterStream(int32_t columnIndex, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }

  /** {inheritDoc}. */
  void SelectResultSetCapi::updateNCharacterStream(const SQLString& columnLabel, std::istringstream& reader) {
    throw ExceptionFactory::INSTANCE.notSupported(NOT_UPDATABLE_ERROR);
  }
#endif

  /** {inheritDoc}. */
  int32_t SelectResultSetCapi::getHoldability() {
    return ResultSet::HOLD_CURSORS_OVER_COMMIT;
  }

#ifdef JDBC_SPECIFIC_TYPES_IMPLEMENTED
  /** {inheritDoc}. */
  bool SelectResultSetCapi::isWrapperFor() {
    return iface.isInstance(this);
  }
#endif

  /** Force metadata getTableName to return table alias, not original table name. */
  void SelectResultSetCapi::setForceTableAlias() {
    this->forceAlias= true;
  }

  void SelectResultSetCapi::rangeCheck(const SQLString& className, int64_t minValue, int64_t maxValue, int64_t value, ColumnDefinition* columnInfo) {
    if (value < minValue || value > maxValue) {
      throw SQLException(
        "Out of range value for column '"
        +columnInfo->getName()
        +"' : value "
        + std::to_string(value)
        +" is not in "
        + className
        +" range",
        "22003",
        1264);
    }
  }

  int32_t SelectResultSetCapi::getRowPointer() {
    return rowPointer;
  }

  void SelectResultSetCapi::setRowPointer(int32_t pointer) {
    rowPointer= pointer;
  }

  int32_t SelectResultSetCapi::getDataSize() {
    return dataSize;
  }

  bool SelectResultSetCapi::isBinaryEncoded() {
    return row->isBinaryEncoded();
  }
}
}
}