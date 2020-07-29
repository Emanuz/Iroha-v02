/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_WSVRESTORERIMPL_HPP
#define IROHA_WSVRESTORERIMPL_HPP

#include "ametsuchi/wsv_restorer.hpp"

#include "ametsuchi/ledger_state.hpp"
#include "common/result.hpp"

namespace iroha {
  namespace ametsuchi {

    /**
     * Recover WSV (World State View).
     * @return true on success, otherwise false
     */
    class WsvRestorerImpl : public WsvRestorer {
     public:
      virtual ~WsvRestorerImpl() = default;
      /**
       * Recover WSV (World State View).
       * Drop storage and apply blocks one by one.
       * @param storage of blocks in ledger
       * @return ledger state after restoration on success, otherwise error
       * string
       */
      CommitResult restoreWsv(Storage &storage) override;
    };

  }  // namespace ametsuchi
}  // namespace iroha

#endif  // IROHA_WSVRESTORERIMPL_HPP
