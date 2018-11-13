/*
bumo is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

bumo is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with bumo.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <glue/glue_manager.h>
#include <overlay/peer_manager.h>
#include "cross_utils.h"

namespace bumo {
	int32_t CrossUtils::QueryContract(const std::string &address, const std::string &input, std::string &result){
		ContractTestParameter parameter;
		parameter.code_ = "";
		parameter.input_ = input;
		parameter.opt_type_ = ContractTestParameter::QUERY;
		parameter.contract_address_ = address;
		parameter.source_address_ = "";
		parameter.fee_limit_ = 1000000000000;
		parameter.gas_price_ = LedgerManager::Instance().GetCurFeeConfig().gas_price();
		parameter.contract_balance_ = 1000000000000;

		int32_t error_code = protocol::ERRCODE_SUCCESS;
		AccountFrm::pointer acc = NULL;
		do {
			if (parameter.contract_address_.empty()) {
				error_code = protocol::ERRCODE_INVALID_PARAMETER;
				result = "Empty contract address";
				LOG_ERROR("%s", result.c_str());
				break;
			}

			if (!Environment::AccountFromDB(parameter.contract_address_, acc)) {
				error_code = protocol::ERRCODE_NOT_EXIST;
				result = utils::String::Format("Account(%s) is not existed", parameter.contract_address_.c_str());
				LOG_ERROR("Failed to load the account from the database. %s", result.c_str());
				break;
			}

			parameter.code_ = acc->GetProtoAccount().contract().payload();

			if (parameter.code_.empty()) {
				error_code = protocol::ERRCODE_NOT_EXIST;
				result = utils::String::Format("Account(%s) has no contract code", parameter.contract_address_.c_str());
				LOG_ERROR("Failed to load test parameter. %s", result.c_str());
				break;
			}

			Result exe_result;
			Json::Value result_json = Json::Value(Json::objectValue);
			if (!LedgerManager::Instance().context_manager_.SyncTestProcess(LedgerContext::AT_TEST_V8,
				(TestParameter*)&parameter,
				utils::MICRO_UNITS_PER_SEC,
				exe_result, result_json["logs"], result_json["txs"], result_json["query_rets"], result_json["stat"])) {
				error_code = exe_result.code();
				result = exe_result.desc();
				LOG_ERROR("Failed to execute the test.%s", result.c_str());
				break;
			}

			result = result_json["query_rets"].toFastString();
		} while (false);

		LOG_INFO("Query result code:%d, result:%s", error_code, result.c_str());

		return error_code;
	}

	int32_t CrossUtils::PayCoin(const std::string &encode_private_key, const std::string &dest_address, const std::string &contract_input, int64_t coin_amount) {
		PrivateKey private_key(encode_private_key);
		if (!private_key.IsValid()){
			LOG_ERROR("Private key is not valid");
			return protocol::ERRCODE_INVALID_PRIKEY;
		}

		int64_t nonce = 0;
		std::string source_address = private_key.GetEncAddress();
		do {
			AccountFrm::pointer account_ptr;
			if (!Environment::AccountFromDB(source_address, account_ptr)) {
				LOG_ERROR("Address:%s not exsit", source_address.c_str());
				return protocol::ERRCODE_INVALID_PRIKEY;
			}
			else {
				nonce = account_ptr->GetAccountNonce() + 1;
			}
		} while (false);

		protocol::TransactionEnv tran_env;
		protocol::Transaction *tran = tran_env.mutable_transaction();
		tran->set_source_address(source_address);
		tran->set_fee_limit(1000000);
		tran->set_gas_price(LedgerManager::Instance().GetCurFeeConfig().gas_price());
		tran->set_nonce(nonce);
		protocol::Operation *ope = tran->add_operations();
		ope->set_type(protocol::Operation_Type_PAY_COIN);
		protocol::OperationPayCoin *pay_coin = ope->mutable_pay_coin();
		pay_coin->set_amount(coin_amount);
		pay_coin->set_dest_address(dest_address);
		pay_coin->set_input(contract_input);

		std::string content = tran->SerializeAsString();
		std::string sign = private_key.Sign(content);
		protocol::Signature *signpro = tran_env.add_signatures();
		signpro->set_sign_data(sign);
		signpro->set_public_key(private_key.GetEncPublicKey());

		Result result;
		TransactionFrm::pointer ptr = std::make_shared<TransactionFrm>(tran_env);
		GlueManager::Instance().OnTransaction(ptr, result);
		if (result.code() != 0) {
			LOG_ERROR("Pay coin result code:%d, des:%s", result.code(), result.desc().c_str());
			return result.code();
		}

		PeerManager::Instance().Broadcast(protocol::OVERLAY_MSGTYPE_TRANSACTION, tran_env.SerializeAsString());

		LOG_INFO("Pay coin tx hash %s", utils::String::BinToHexString(HashWrapper::Crypto(content)).c_str());
		return protocol::ERRCODE_SUCCESS;
	}
}