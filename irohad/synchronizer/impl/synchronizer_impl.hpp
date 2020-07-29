/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_SYNCHRONIZER_IMPL_HPP
#define IROHA_SYNCHRONIZER_IMPL_HPP

#include "synchronizer/synchronizer.hpp"

#include <rxcpp/rx-lite.hpp>
#include "ametsuchi/commit_result.hpp"
#include "ametsuchi/mutable_factory.hpp"
#include "ametsuchi/peer_query_factory.hpp"
#include "logger/logger_fwd.hpp"
#include "network/block_loader.hpp"
#include "network/consensus_gate.hpp"
#include "validation/chain_validator.hpp"

namespace iroha {

  namespace ametsuchi {
    class BlockQueryFactory;
    class CommandExecutor;
  }  // namespace ametsuchi

  namespace synchronizer {

    class SynchronizerImpl : public Synchronizer {
     public:
      SynchronizerImpl(
          std::unique_ptr<iroha::ametsuchi::CommandExecutor> command_executor,
          std::shared_ptr<network::ConsensusGate> consensus_gate,
          std::shared_ptr<validation::ChainValidator> validator,
          std::shared_ptr<ametsuchi::MutableFactory> mutable_factory,
          std::shared_ptr<ametsuchi::BlockQueryFactory> block_query_factory,
          std::shared_ptr<network::BlockLoader> block_loader,
          logger::LoggerPtr log);

      ~SynchronizerImpl() override;

      void processOutcome(consensus::GateObject object) override;
      rxcpp::observable<SynchronizationEvent> on_commit_chain() override;

     private:
      /**
       * Iterate through the peers which signed the commit message, load and
       * apply the missing blocks
       * @param start_height - the block from which to start synchronization
       * @param target_height - the block height that must be reached
       * @param public_keys - public keys of peers from which to ask the blocks
       * @return Result of committing the downloaded blocks.
       */
      ametsuchi::CommitResult downloadAndCommitMissingBlocks(
          const shared_model::interface::types::HeightType start_height,
          const shared_model::interface::types::HeightType target_height,
          const shared_model::interface::types::PublicKeyCollectionType
              &public_keys);

      void processNext(const consensus::PairValid &msg);

      /**
       * Performs synchronization on rejects
       * @param msg - consensus gate message with a list of peers and a round
       * @param required_height - minimal top block height to be downloaded
       */
      void processDifferent(
          const consensus::Synchronizable &msg,
          shared_model::interface::types::HeightType required_height);

      std::unique_ptr<ametsuchi::MutableStorage> getStorage();

      std::shared_ptr<iroha::ametsuchi::CommandExecutor> command_executor_;

      std::shared_ptr<validation::ChainValidator> validator_;
      std::shared_ptr<ametsuchi::MutableFactory> mutable_factory_;
      std::shared_ptr<ametsuchi::BlockQueryFactory> block_query_factory_;
      std::shared_ptr<network::BlockLoader> block_loader_;

      // internal
      rxcpp::composite_subscription notifier_lifetime_;
      rxcpp::subjects::subject<SynchronizationEvent> notifier_;
      rxcpp::composite_subscription subscription_;

      logger::LoggerPtr log_;
    };

  }  // namespace synchronizer
}  // namespace iroha

#endif  // IROHA_SYNCHRONIZER_IMPL_HPP
