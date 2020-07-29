/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_BLOCK_LOADER_IMPL_HPP
#define IROHA_BLOCK_LOADER_IMPL_HPP

#include "network/block_loader.hpp"

#include <unordered_map>

#include "ametsuchi/peer_query_factory.hpp"
#include "backend/protobuf/proto_block_factory.hpp"
#include "interfaces/common_objects/string_view_types.hpp"
#include "loader.grpc.pb.h"
#include "logger/logger_fwd.hpp"

namespace iroha {
  namespace network {
    class BlockLoaderImpl : public BlockLoader {
     public:
      // TODO 30.01.2019 lebdron: IR-264 Remove PeerQueryFactory
      BlockLoaderImpl(
          std::shared_ptr<ametsuchi::PeerQueryFactory> peer_query_factory,
          shared_model::proto::ProtoBlockFactory factory,
          logger::LoggerPtr log);

      rxcpp::observable<std::shared_ptr<shared_model::interface::Block>>
      retrieveBlocks(const shared_model::interface::types::HeightType height,
                     shared_model::interface::types::PublicKeyHexStringView
                         peer_pubkey) override;

      boost::optional<std::shared_ptr<shared_model::interface::Block>>
      retrieveBlock(
          shared_model::interface::types::PublicKeyHexStringView peer_pubkey,
          shared_model::interface::types::HeightType block_height) override;

     private:
      /**
       * Retrieve peers from database, and find the requested peer by pubkey
       * @param pubkey - public key of requested peer
       * @return peer, if it was found, otherwise nullopt
       * TODO 14/02/17 (@l4l) IR-960 rework method with returning result
       */
      boost::optional<std::shared_ptr<shared_model::interface::Peer>> findPeer(
          shared_model::interface::types::PublicKeyHexStringView pubkey);
      /**
       * Get or create a RPC stub for connecting to peer
       * @param peer for connecting
       * @return RPC stub
       */
      proto::Loader::StubInterface &getPeerStub(
          const shared_model::interface::Peer &peer);

      std::unordered_map<shared_model::interface::types::AddressType,
                         std::unique_ptr<proto::Loader::StubInterface>>
          peer_connections_;
      std::shared_ptr<ametsuchi::PeerQueryFactory> peer_query_factory_;
      shared_model::proto::ProtoBlockFactory block_factory_;

      logger::LoggerPtr log_;
    };
  }  // namespace network
}  // namespace iroha

#endif  // IROHA_BLOCK_LOADER_IMPL_HPP
