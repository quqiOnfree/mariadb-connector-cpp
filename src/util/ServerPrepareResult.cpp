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


#include "ServerPrepareResult.h"

#include "ColumnType.h"
#include "ColumnDefinition.h"
#include "parameters/ParameterHolder.h"

#include "com/capi/ColumnDefinitionCapi.h"

namespace sql
{
namespace mariadb
{

  /**
    * PrepareStatement Result object.
    *
    * @param sql query
    * @param statementId server statement Id.
    * @param columns columns information
    * @param parameters parameters information
    * @param unProxiedProtocol indicate the protocol on which the prepare has been done
    */
  ServerPrepareResult::ServerPrepareResult(
    SQLString _sql,
    capi::MYSQL_STMT* _statementId,
    std::vector<Shared::ColumnDefinition>& _columns,
    std::vector<Shared::ColumnDefinition>& _parameters,
    Protocol* _unProxiedProtocol)

    : sql(_sql)
    , statementId(_statementId)
    , columns(_columns)
    , parameters(_parameters)
    , unProxiedProtocol(_unProxiedProtocol)
    , metadata(mysql_stmt_result_metadata(statementId), &capi::mysql_free_result)
  {
  }

  /**
  * PrepareStatement Result object.
  *
  * @param sql query
  * @param statementId server statement Id.
  * @param columns columns information
  * @param parameters parameters information
  * @param unProxiedProtocol indicate the protocol on which the prepare has been done
  */
  ServerPrepareResult::ServerPrepareResult(
    SQLString _sql,
    capi::MYSQL_STMT* _statementId,
    Protocol* _unProxiedProtocol)
    : sql(_sql)
    , statementId(_statementId)
    , unProxiedProtocol(_unProxiedProtocol)
    , metadata(mysql_stmt_result_metadata(statementId), &capi::mysql_free_result)
  {
    columns.reserve(mysql_stmt_field_count(statementId));

    for (uint32_t i= 0; i < mysql_stmt_field_count(statementId); ++i) {
      columns.emplace_back(new capi::ColumnDefinitionCapi(mysql_fetch_field_direct(metadata.get(), i)));
    }
    parameters.reserve(mysql_stmt_param_count(statementId));

    for (uint32_t i= 0; i < mysql_stmt_param_count(statementId); ++i) {
      parameters.emplace_back();
    }
  }


  void ServerPrepareResult::reReadColumnInfo()
  {
    metadata.reset(mysql_stmt_result_metadata(statementId));

    for (uint32_t i= 0; i < mysql_stmt_field_count(statementId); ++i) {
      if (i >= columns.size()) {
        columns.emplace_back(new capi::ColumnDefinitionCapi(mysql_fetch_field_direct(metadata.get(), i)));
      }
      else {
        columns[i].reset(new capi::ColumnDefinitionCapi(mysql_fetch_field_direct(metadata.get(), i)));
      }
    }
  }


  void ServerPrepareResult::resetParameterTypeHeader()
  {
    this->paramBind.clear();

    if (parameters.size() > 0) {
      this->paramBind.reserve(parameters.size());
      for (uint32_t i= 0; i < parameters.size(); ++i) {
        paramBind.emplace_back();
      }
    }
  }

  /**
    * Update information after a failover.
    *
    * @param statementId new statement Id
    * @param unProxiedProtocol the protocol on which the prepare has been done
    */
  void ServerPrepareResult::failover(capi::MYSQL_STMT* statementId, Shared::Protocol& unProxiedProtocol)
  {
    this->statementId= statementId;
    this->unProxiedProtocol= unProxiedProtocol.get();
    resetParameterTypeHeader();
    this->shareCounter= 1;
    this->isBeingDeallocate= false;
  }

  void ServerPrepareResult::setAddToCache()
  {
    inCache.store(true);
  }

  void ServerPrepareResult::setRemoveFromCache()
  {
    inCache.store(false);
  }

  /**
    * Increment share counter.
    *
    * @return true if can be used (is not been deallocate).
    */
  bool ServerPrepareResult::incrementShareCounter()
  {
    std::lock_guard<std::mutex> localScopeLock(lock);
    if (isBeingDeallocate) {
      return false;
    }

    shareCounter++;
    return true;
  }

  void ServerPrepareResult::decrementShareCounter()
  {
    std::lock_guard<std::mutex> localScopeLock(lock);
    shareCounter--;
  }

  /**
    * Asked if can be deallocate (is not shared in other statement and not in cache) Set deallocate
    * flag to true if so.
    *
    * @return true if can be deallocate
    */
  bool ServerPrepareResult::canBeDeallocate()
  {
    std::lock_guard<std::mutex> localScopeLock(lock);

    if (shareCounter >0 || isBeingDeallocate) {
      return false;
    }
    if (!inCache.load()) {
      isBeingDeallocate= true;
      return true;
    }
    return false;
  }

  size_t ServerPrepareResult::getParamCount() const
  {
    return parameters.size();
  }

  // for unit test
  int32_t ServerPrepareResult::getShareCounter()
  {
    std::lock_guard<std::mutex> localScopeLock(lock);
    return shareCounter;
  }

  capi::MYSQL_STMT* ServerPrepareResult::getStatementId()
  {
    return statementId;
  }

  const std::vector<Shared::ColumnDefinition>& ServerPrepareResult::getColumns() const
  {
    return columns;
  }

  const std::vector<Shared::ColumnDefinition>& ServerPrepareResult::getParameters() const
  {
    return parameters;
  }


  Protocol* ServerPrepareResult::getUnProxiedProtocol()
  {
    return unProxiedProtocol;
  }


  const SQLString& ServerPrepareResult::getSql() const
  {
    return sql;
  }


  const std::vector<capi::MYSQL_BIND>& ServerPrepareResult::getParameterTypeHeader() const
  {
    return paramBind;
  }


  void initBindStruct(capi::MYSQL_BIND& bind, const ParameterHolder& paramInfo)
  {
    const ColumnType& typeInfo= paramInfo.getColumnType();
    std::memset(&bind, 0, sizeof(bind));

    bind.buffer_type= static_cast<capi::enum_field_types>(typeInfo.getType());
    bind.is_null= &bind.is_null_value;
  }


  void bindParamValue(capi::MYSQL_BIND& bind, Shared::ParameterHolder& param)
  {
    bind.is_null_value= '\0';
    bind.long_data_used= '\0';

    if (param->isNullData()) {
      bind.is_null_value= '\1';
      return;
    }

    if (param->isLongData()) {
      bind.long_data_used= '\1';
      return;
    }

    if (param->isUnsigned()) {
      bind.is_unsigned= '\1';
    }

    bind.buffer= param->getValuePtr();
    bind.buffer_length=param->getValueBinLen();
  }


  void ServerPrepareResult::bindParameters(std::vector<Shared::ParameterHolder>& paramValue)
  {
    for (size_t i= 0; i < parameters.size(); ++i)
    {
      auto& bind= paramBind[i];
      std::memset(&bind, 0, sizeof(capi::MYSQL_BIND));
      initBindStruct(bind, *paramValue[i]);

      bindParamValue(bind, paramValue[i]);
    }
    capi::mysql_stmt_bind_param(statementId, paramBind.data());
  }


  void ServerPrepareResult::bindParameters(std::vector<std::vector<Shared::ParameterHolder>>& paramValue)
  {
    for (auto& paramRow : paramValue)
    {
      uint32_t i= 0;
      for (auto& bind : paramBind)
      {
        std::memset(&bind, 0, sizeof(capi::MYSQL_BIND));
        initBindStruct(bind, *paramRow[i]);

        bindParamValue(bind, paramRow[i]);
        ++i;
      }
    }
    capi::mysql_stmt_bind_param(statementId, paramBind.data());
  }
}
}
