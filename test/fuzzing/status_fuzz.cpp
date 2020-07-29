/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "torii/impl/command_service_transport_grpc.hpp"

#include <memory>

#include <gtest/gtest.h>
#include <libfuzzer/libfuzzer_macro.h>
#include "ametsuchi/tx_cache_response.hpp"
#include "backend/protobuf/proto_transport_factory.hpp"
#include "backend/protobuf/proto_tx_status_factory.hpp"
#include "backend/protobuf/transaction.hpp"
#include "interfaces/iroha_internal/transaction_batch_factory_impl.hpp"
#include "interfaces/iroha_internal/transaction_batch_parser_impl.hpp"
#include "logger/dummy_logger.hpp"
#include "module/irohad/ametsuchi/ametsuchi_mocks.hpp"
#include "module/irohad/ametsuchi/mock_tx_presence_cache.hpp"
#include "module/irohad/common/validators_config.hpp"
#include "module/irohad/multi_sig_transactions/mst_mocks.hpp"
#include "module/irohad/network/network_mocks.hpp"
#include "synchronizer/synchronizer_common.hpp"
#include "torii/impl/command_service_impl.hpp"
#include "torii/impl/status_bus_impl.hpp"
#include "torii/processor/transaction_processor_impl.hpp"
#include "validators/default_validator.hpp"
#include "validators/protobuf/proto_transaction_validator.hpp"

using testing::_;
using testing::Return;

struct CommandFixture {
  std::shared_ptr<iroha::torii::CommandService> service_;
  std::shared_ptr<iroha::torii::CommandServiceTransportGrpc> service_transport_;
  std::shared_ptr<iroha::torii::TransactionProcessorImpl> tx_processor_;
  std::shared_ptr<iroha::ametsuchi::MockStorage> storage_;
  std::shared_ptr<iroha::network::MockPeerCommunicationService> pcs_;
  std::shared_ptr<iroha::MockMstProcessor> mst_processor_;
  std::shared_ptr<iroha::ametsuchi::MockBlockQuery> bq_;
  std::vector<iroha::torii::CommandServiceTransportGrpc::ConsensusGateEvent>
      consensus_gate_objects_{2};
  std::shared_ptr<iroha::torii::CommandServiceImpl::CacheType> cache_;
  std::shared_ptr<iroha::ametsuchi::MockTxPresenceCache> tx_presence_cache_;

  rxcpp::subjects::subject<iroha::network::OrderingEvent> prop_notifier_;
  rxcpp::subjects::subject<iroha::simulator::VerifiedProposalCreatorEvent>
      vprop_notifier_;
  rxcpp::subjects::subject<
      std::shared_ptr<const shared_model::interface::Block>>
      commit_notifier_;
  rxcpp::subjects::subject<iroha::DataType> mst_notifier_;
  rxcpp::subjects::subject<std::shared_ptr<iroha::MstState>>
      mst_state_notifier_;
  rxcpp::subjects::subject<iroha::consensus::GateObject> consensus_notifier_;

  CommandFixture() {
    pcs_ = std::make_shared<iroha::network::MockPeerCommunicationService>();
    EXPECT_CALL(*pcs_, onProposal())
        .WillRepeatedly(Return(prop_notifier_.get_observable()));
    EXPECT_CALL(*pcs_, onVerifiedProposal())
        .WillRepeatedly(Return(vprop_notifier_.get_observable()));

    mst_processor_ =
        std::make_shared<iroha::MockMstProcessor>(logger::getDummyLoggerPtr());
    EXPECT_CALL(*mst_processor_, onStateUpdateImpl())
        .WillRepeatedly(Return(mst_state_notifier_.get_observable()));
    EXPECT_CALL(*mst_processor_, onPreparedBatchesImpl())
        .WillRepeatedly(Return(mst_notifier_.get_observable()));
    EXPECT_CALL(*mst_processor_, onExpiredBatchesImpl())
        .WillRepeatedly(Return(mst_notifier_.get_observable()));

    auto status_bus = std::make_shared<iroha::torii::StatusBusImpl>();
    auto status_factory =
        std::make_shared<shared_model::proto::ProtoTxStatusFactory>();
    tx_processor_ = std::make_shared<iroha::torii::TransactionProcessorImpl>(
        pcs_,
        mst_processor_,
        status_bus,
        status_factory,
        commit_notifier_.get_observable(),
        logger::getDummyLoggerPtr());

    std::unique_ptr<shared_model::validation::AbstractValidator<
        shared_model::interface::Transaction>>
        transaction_validator =
            std::make_unique<shared_model::validation::
                                 DefaultOptionalSignedTransactionValidator>(
                iroha::test::kTestsValidatorsConfig);
    std::unique_ptr<shared_model::validation::AbstractValidator<
        iroha::protocol::Transaction>>
        proto_transaction_validator = std::make_unique<
            shared_model::validation::ProtoTransactionValidator>();
    std::shared_ptr<shared_model::interface::AbstractTransportFactory<
        shared_model::interface::Transaction,
        iroha::protocol::Transaction>>
        transaction_factory =
            std::make_shared<shared_model::proto::ProtoTransportFactory<
                shared_model::interface::Transaction,
                shared_model::proto::Transaction>>(
                std::move(transaction_validator),
                std::move(proto_transaction_validator));
    std::shared_ptr<shared_model::interface::TransactionBatchParser>
        batch_parser = std::make_shared<
            shared_model::interface::TransactionBatchParserImpl>();
    std::shared_ptr<shared_model::validation::AbstractValidator<
        shared_model::interface::TransactionBatch>>
        batch_validator =
            std::make_shared<shared_model::validation::DefaultBatchValidator>(
                iroha::test::kTestsValidatorsConfig);
    std::shared_ptr<shared_model::interface::TransactionBatchFactory>
        transaction_batch_factory = std::make_shared<
            shared_model::interface::TransactionBatchFactoryImpl>(
            batch_validator);

    storage_ = std::make_shared<iroha::ametsuchi::MockStorage>();
    bq_ = std::make_shared<iroha::ametsuchi::MockBlockQuery>();
    EXPECT_CALL(*storage_, getBlockQuery()).WillRepeatedly(Return(bq_));
    tx_presence_cache_ =
        std::make_shared<iroha::ametsuchi::MockTxPresenceCache>();
    cache_ = std::make_shared<iroha::torii::CommandServiceImpl::CacheType>();
    service_ = std::make_shared<iroha::torii::CommandServiceImpl>(
        tx_processor_,
        storage_,
        status_bus,
        status_factory,
        cache_,
        tx_presence_cache_,
        logger::getDummyLoggerPtr());
    service_transport_ =
        std::make_shared<iroha::torii::CommandServiceTransportGrpc>(
            service_,
            status_bus,
            status_factory,
            transaction_factory,
            batch_parser,
            transaction_batch_factory,
            rxcpp::observable<>::iterate(consensus_gate_objects_),
            2,
            logger::getDummyLoggerPtr());
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
  static CommandFixture handler;
  if (size < 1) {
    return 0;
  }
  std::optional<iroha::ametsuchi::TxCacheStatusType> presense;
  using namespace iroha::ametsuchi::tx_cache_status_responses;
  switch (data[0] % 4) {
    case 0:
      presense = {};
      break;
    case 1:
      presense = std::make_optional(Committed{});
      break;
    case 2:
      presense = std::make_optional(Rejected{});
      break;
    case 3:
      presense = std::make_optional(Missing{});
      break;
  }
  EXPECT_CALL(*handler.bq_, checkTxPresence(_))
      .WillRepeatedly(Return(presense));
  iroha::protocol::TxStatusRequest tx;
  if (protobuf_mutator::libfuzzer::LoadProtoInput(
          true, data + 1, size - 1, &tx)) {
    iroha::protocol::ToriiResponse resp;
    handler.service_transport_->Status(nullptr, &tx, &resp);
  }
  return 0;
}
