/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_IROHA_INSTANCE_HPP
#define IROHA_IROHA_INSTANCE_HPP

#include <chrono>
#include <memory>
#include <string>

#include <boost/optional.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "ametsuchi/impl/postgres_options.hpp"
#include "logger/logger_fwd.hpp"
#include "logger/logger_manager_fwd.hpp"
#include "main/startup_params.hpp"
#include "multi_sig_transactions/gossip_propagation_strategy_params.hpp"
#include "torii/tls_params.hpp"

namespace shared_model {
  namespace interface {
    class Block;
    class Peer;
  }  // namespace interface
  namespace crypto {
    class Keypair;
  }  // namespace crypto
}  // namespace shared_model

namespace integration_framework {
  class TestIrohad;

  class IrohaInstance {
   public:
    /**
     * @param mst_support enables multisignature tx support
     * @param block_store_path
     * @param listen_ip - ip address for opening ports (internal & torii)
     * @param torii_port - port to bind Torii service to
     * @param internal_port - port for internal irohad communication
     * @param irohad_log_manager - the log manager for irohad
     * @param log - the log for internal messages
     * @param startup_wsv_data_policy - @see StartupWsvDataPolicy
     * @param dbname is a name of postgres database
     * @param tls_params - optional tls parameters for torii
     *   @see iroha::torii::TlsParams
     */
    IrohaInstance(bool mst_support,
                  const boost::optional<std::string> &block_store_path,
                  const std::string &listen_ip,
                  size_t torii_port,
                  size_t internal_port,
                  logger::LoggerManagerTreePtr irohad_log_manager,
                  logger::LoggerPtr log,
                  iroha::StartupWsvDataPolicy startup_wsv_data_policy,
                  const boost::optional<std::string> &dbname = boost::none,
                  const boost::optional<iroha::torii::TlsParams> &tls_params =
                      boost::none);

    /// Initialize Irohad. Throws on error.
    void init();

    void makeGenesis(
        std::shared_ptr<const shared_model::interface::Block> block);

    void rawInsertBlock(
        std::shared_ptr<const shared_model::interface::Block> block);

    void setMstGossipParams(
        std::chrono::milliseconds mst_gossip_emitting_period,
        uint32_t mst_gossip_amount_per_once);

    void initPipeline(const shared_model::crypto::Keypair &key_pair,
                      size_t max_proposal_size = 10);

    void run();

    // TODO mboldyrev 09.11.2018 IrohaInstance::getIrohaInstance() looks weird,
    //      IR-1885              refactoring requested.
    std::shared_ptr<TestIrohad> &getIrohaInstance();

    /// Terminate Iroha instance and clean the resources up.
    void terminateAndCleanup();

    // config area
    const boost::optional<std::string> block_store_dir_;
    const std::string working_dbname_;
    const std::string listen_ip_;
    const size_t torii_port_;
    boost::optional<iroha::torii::TlsParams> torii_tls_params_;
    const size_t internal_port_;
    const std::chrono::milliseconds proposal_delay_;
    const std::chrono::milliseconds vote_delay_;
    const std::chrono::minutes mst_expiration_time_;
    boost::optional<iroha::GossipPropagationStrategyParams>
        opt_mst_gossip_params_;
    const std::chrono::milliseconds max_rounds_delay_;
    const size_t stale_stream_max_rounds_;

   private:
    std::shared_ptr<TestIrohad> instance_;
    logger::LoggerManagerTreePtr irohad_log_manager_;

    logger::LoggerPtr log_;

    const iroha::StartupWsvDataPolicy startup_wsv_data_policy_;
  };
}  // namespace integration_framework
#endif  // IROHA_IROHA_INSTANCE_HPP
