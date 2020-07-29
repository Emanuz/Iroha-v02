/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_MOCK_MUTABLE_STORAGE_HPP
#define IROHA_MOCK_MUTABLE_STORAGE_HPP

#include "ametsuchi/mutable_storage.hpp"

#include <gmock/gmock.h>
#include <rxcpp/rx-lite.hpp>

namespace iroha {
  namespace ametsuchi {

    class MockMutableStorage : public MutableStorage {
     public:
      MOCK_METHOD2(
          apply,
          bool(rxcpp::observable<
                   std::shared_ptr<shared_model::interface::Block>>,
               std::function<
                   bool(std::shared_ptr<const shared_model::interface::Block>,
                        const iroha::LedgerState &)>));
      MOCK_METHOD1(apply,
                   bool(std::shared_ptr<const shared_model::interface::Block>));
      MOCK_METHOD1(applyPrepared,
                   bool(std::shared_ptr<const shared_model::interface::Block>));
      MOCK_METHOD0(
          do_commit,
          expected::Result<MutableStorage::CommitResult, std::string>());

      expected::Result<MutableStorage::CommitResult, std::string> commit()
          && override {
        return do_commit();
      }
    };

  }  // namespace ametsuchi
}  // namespace iroha

#endif  // IROHA_MOCK_MUTABLE_STORAGE_HPP
