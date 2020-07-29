/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_BLOCK_LOADER_HPP
#define IROHA_BLOCK_LOADER_HPP

#include <memory>
#include <rxcpp/rx-observable-fwd.hpp>

#include "interfaces/common_objects/types.hpp"
#include "interfaces/iroha_internal/block.hpp"

namespace iroha {
  namespace network {
    /**
     * Interface for downloading blocks from a network
     */
    class BlockLoader {
     public:
      /**
       * Retrieve block from given peer starting from current top
       * @param height - top block height in requester's peer storage
       * @param peer_pubkey - peer for requesting blocks
       * @return
       */
      virtual rxcpp::observable<std::shared_ptr<shared_model::interface::Block>>
      retrieveBlocks(const shared_model::interface::types::HeightType height,
                     shared_model::interface::types::PublicKeyHexStringView
                         peer_pubkey) = 0;

      /**
       * Retrieve block by its block_height from given peer
       * @param peer_pubkey - peer for requesting blocks
       * @param block_height - requested block height
       * @return block on success, nullopt on failure
       * TODO 14/02/17 (@l4l) IR-960 rework method with returning result
       */
      virtual boost::optional<std::shared_ptr<shared_model::interface::Block>>
      retrieveBlock(
          shared_model::interface::types::PublicKeyHexStringView peer_pubkey,
          shared_model::interface::types::HeightType block_height) = 0;

      virtual ~BlockLoader() = default;
    };
  }  // namespace network
}  // namespace iroha

#endif  // IROHA_BLOCK_LOADER_HPP
