/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_POSTGRES_QUERY_EXECUTOR_HPP
#define IROHA_POSTGRES_QUERY_EXECUTOR_HPP

#include "ametsuchi/query_executor.hpp"

#include <soci/soci.h>
#include "logger/logger_fwd.hpp"

namespace shared_model {
  namespace interface {
    class QueryResponseFactory;
  }  // namespace interface
}  // namespace shared_model

namespace iroha {
  namespace ametsuchi {

    class SpecificQueryExecutor;

    class PostgresQueryExecutor : public QueryExecutor {
     public:
      PostgresQueryExecutor(
          std::unique_ptr<soci::session> sql,
          std::shared_ptr<shared_model::interface::QueryResponseFactory>
              response_factory,
          std::shared_ptr<SpecificQueryExecutor> specific_query_executor,
          logger::LoggerPtr log);

      QueryExecutorResult validateAndExecute(
          const shared_model::interface::Query &query,
          const bool validate_signatories) override;

      bool validate(const shared_model::interface::BlocksQuery &query,
                    const bool validate_signatories) override;

     private:
      template <class Q>
      bool validateSignatures(const Q &query);

      std::unique_ptr<soci::session> sql_;
      std::shared_ptr<SpecificQueryExecutor> specific_query_executor_;
      std::shared_ptr<shared_model::interface::QueryResponseFactory>
          query_response_factory_;
      logger::LoggerPtr log_;
    };

  }  // namespace ametsuchi
}  // namespace iroha

#endif  // IROHA_POSTGRES_QUERY_EXECUTOR_HPP
