/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "interfaces/common_objects/string_view_types.hpp"
#include "synchronizer/impl/synchronizer_impl.hpp"

#include <string_view>

#include <gmock/gmock-generated-matchers.h>
#include <gmock/gmock.h>
#include <boost/range/adaptor/transformed.hpp>
#include <rxcpp/operators/rx-take.hpp>
#include "backend/protobuf/block.hpp"
#include "framework/test_logger.hpp"
#include "framework/test_subscriber.hpp"
#include "module/irohad/ametsuchi/mock_block_query.hpp"
#include "module/irohad/ametsuchi/mock_block_query_factory.hpp"
#include "module/irohad/ametsuchi/mock_command_executor.hpp"
#include "module/irohad/ametsuchi/mock_mutable_factory.hpp"
#include "module/irohad/ametsuchi/mock_mutable_storage.hpp"
#include "module/irohad/network/network_mocks.hpp"
#include "module/irohad/validation/validation_mocks.hpp"
#include "module/shared_model/builders/protobuf/block.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/cryptography/crypto_defaults.hpp"
#include "module/shared_model/interface_mocks.hpp"
#include "validation/chain_validator.hpp"

using namespace iroha;
using namespace iroha::ametsuchi;
using namespace iroha::synchronizer;
using namespace iroha::validation;
using namespace iroha::network;
using namespace framework::test_subscriber;
using namespace shared_model::interface::types;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::ByRef;
using ::testing::DefaultValue;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Return;

using Chain =
    rxcpp::observable<std::shared_ptr<shared_model::interface::Block>>;

/**
 * Factory for mock mutable storage generation.
 * This method provides technique,
 * when required to return object wrapped in Result.
 */
expected::Result<std::unique_ptr<MutableStorage>, std::string>
createMockMutableStorage() {
  return expected::makeValue<std::unique_ptr<MutableStorage>>(
      std::make_unique<MockMutableStorage>());
}

static constexpr shared_model::interface::types::HeightType kHeight{5};
static constexpr shared_model::interface::types::HeightType kInitTopBlockHeight{
    kHeight - 1};

class SynchronizerTest : public ::testing::Test {
 public:
  void SetUp() override {
    chain_validator = std::make_shared<MockChainValidator>();
    auto command_executor = std::make_unique<MockCommandExecutor>();
    mutable_factory = std::make_shared<MockMutableFactory>();
    block_query_factory =
        std::make_shared<::testing::NiceMock<MockBlockQueryFactory>>();
    block_loader = std::make_shared<MockBlockLoader>();
    consensus_gate = std::make_shared<MockConsensusGate>();
    block_query = std::make_shared<::testing::NiceMock<MockBlockQuery>>();

    for (int i = 0; i < 3; ++i) {
      // TODO mboldyrev 21.03.2019 IR-424 Avoid using honest crypto
      ledger_peer_keys.push_back(
          shared_model::crypto::DefaultCryptoAlgorithmType::generateKeypair());
      using shared_model::interface::types::PublicKeyHexStringView;
      ledger_peers.push_back(makePeer(
          std::to_string(i),
          PublicKeyHexStringView{ledger_peer_keys.back().publicKey()}));
    }

    commit_message = makeCommit();
    public_keys = boost::copy_range<
        shared_model::interface::types::PublicKeyCollectionType>(
        commit_message->signatures()
        | boost::adaptors::transformed(
              [](auto &signature) { return signature.publicKey(); }));
    hash = commit_message->hash();

    EXPECT_CALL(*consensus_gate, onOutcome())
        .WillOnce(Return(gate_outcome.get_observable()));

    ON_CALL(*block_query_factory, createBlockQuery())
        .WillByDefault(Return(boost::make_optional(
            std::shared_ptr<iroha::ametsuchi::BlockQuery>(block_query))));
    ON_CALL(*block_query, getTopBlockHeight())
        .WillByDefault(Return(kInitTopBlockHeight));
    ON_CALL(*mutable_factory, commit_(_))
        .WillByDefault(Return(ByMove(expected::makeValue(
            std::make_shared<LedgerState>(ledger_peers,
                                          commit_message->height(),
                                          commit_message->hash())))));
    EXPECT_CALL(*mutable_factory, preparedCommitEnabled())
        .WillRepeatedly(Return(false));
    EXPECT_CALL(*mutable_factory, commitPrepared(_)).Times(0);

    synchronizer =
        std::make_shared<SynchronizerImpl>(std::move(command_executor),
                                           consensus_gate,
                                           chain_validator,
                                           mutable_factory,
                                           block_query_factory,
                                           block_loader,
                                           getTestLogger("Synchronizer"));

    ledger_state = std::make_shared<LedgerState>(
        ledger_peers, commit_message->height() - 1, commit_message->prevHash());
  }

  std::shared_ptr<shared_model::interface::Block> makeCommit(
      shared_model::interface::types::HeightType height = kHeight,
      size_t time = iroha::time::now()) const {
    shared_model::proto::UnsignedWrapper<shared_model::proto::Block> block{
        TestUnsignedBlockBuilder().height(height).createdTime(time).build()};
    for (const auto &key : ledger_peer_keys) {
      block.signAndAddSignature(key);
    }
    return std::make_shared<shared_model::proto::Block>(
        std::move(block).finish());
  }

  std::shared_ptr<MockChainValidator> chain_validator;
  std::shared_ptr<MockMutableFactory> mutable_factory;
  std::shared_ptr<MockBlockQueryFactory> block_query_factory;
  std::shared_ptr<MockBlockLoader> block_loader;
  std::shared_ptr<MockConsensusGate> consensus_gate;
  std::shared_ptr<MockBlockQuery> block_query;

  std::shared_ptr<shared_model::interface::Block> commit_message;
  shared_model::interface::types::PublicKeyCollectionType public_keys;
  shared_model::interface::types::HashType hash;
  shared_model::interface::types::PeerList ledger_peers;
  std::shared_ptr<LedgerState> ledger_state;
  std::vector<shared_model::crypto::Keypair> ledger_peer_keys;

  rxcpp::subjects::subject<ConsensusGate::GateObject> gate_outcome;

  std::shared_ptr<SynchronizerImpl> synchronizer;
};

class ChainMatcher : public ::testing::MatcherInterface<Chain> {
 public:
  explicit ChainMatcher(
      std::vector<std::shared_ptr<shared_model::interface::Block>>
          expected_chain)
      : expected_chain_(std::move(expected_chain)) {}

  bool MatchAndExplain(
      Chain test_chain,
      ::testing::MatchResultListener *listener) const override {
    size_t got_blocks = 0;
    auto wrapper =
        make_test_subscriber<CallExact>(test_chain, expected_chain_.size());
    wrapper.subscribe([this, &got_blocks](const auto &block) {
      EXPECT_LT(got_blocks, expected_chain_.size())
          << "Tested chain provides more blocks than expected";
      if (got_blocks < expected_chain_.size()) {
        const auto &expected_block = expected_chain_[got_blocks];
        EXPECT_THAT(*block, Eq(ByRef(*expected_block)))
            << "Block number " << got_blocks << " does not match!";
      }
      ++got_blocks;
    });
    return wrapper.validate();
  }

  virtual void DescribeTo(::std::ostream *os) const {
    *os << "Tested chain matches expected chain.";
  }

  virtual void DescribeNegationTo(::std::ostream *os) const {
    *os << "Tested chain does not match expected chain.";
  }

 private:
  const std::vector<std::shared_ptr<shared_model::interface::Block>>
      expected_chain_;
};

inline ::testing::Matcher<Chain> ChainEq(
    std::vector<std::shared_ptr<shared_model::interface::Block>>
        expected_chain) {
  return ::testing::MakeMatcher(new ChainMatcher(expected_chain));
}

void mutableStorageExpectChain(
    iroha::ametsuchi::MockMutableFactory &mutable_factory,
    std::vector<std::shared_ptr<shared_model::interface::Block>> chain) {
  const bool must_create_storage = not chain.empty();
  auto create_mutable_storage =
      [chain = std::move(chain)](auto) -> std::unique_ptr<MutableStorage> {
    auto mutable_storage = std::make_unique<MockMutableStorage>();
    if (chain.empty()) {
      EXPECT_CALL(*mutable_storage, apply(_)).Times(0);
    } else {
      InSequence s;  // ensures the call order
      for (const auto &block : chain) {
        EXPECT_CALL(
            *mutable_storage,
            apply(std::const_pointer_cast<const shared_model::interface::Block>(
                block)))
            .WillOnce(Return(true));
      }
    }
    return mutable_storage;
  };
  if (must_create_storage) {
    EXPECT_CALL(mutable_factory, createMutableStorage(_))
        .Times(AtLeast(1))
        .WillRepeatedly(::testing::Invoke(create_mutable_storage));
  } else {
    EXPECT_CALL(mutable_factory, createMutableStorage(_))
        .WillRepeatedly(::testing::Invoke(create_mutable_storage));
  }
}

/**
 * @given A commit from consensus and initialized components
 * @when a valid block that can be applied
 * @then Successful commit
 */
TEST_F(SynchronizerTest, ValidWhenSingleCommitSynchronized) {
  EXPECT_CALL(*mutable_factory, preparedCommitEnabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mutable_factory, commitPrepared(_)).Times(0);
  mutableStorageExpectChain(*mutable_factory, {commit_message});
  EXPECT_CALL(*chain_validator, validateAndApply(_, _)).Times(0);
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _)).Times(0);

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([this](auto commit_event) {
    EXPECT_EQ(this->ledger_peers, commit_event.ledger_state->ledger_peers);
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
  });

  gate_outcome.get_subscriber().on_next(consensus::PairValid(
      consensus::Round{kHeight, 1}, ledger_state, commit_message));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given A commit from consensus and initialized components
 * @when gate have voted for other block
 * @then Successful commit
 */
TEST_F(SynchronizerTest, ValidWhenValidChain) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);
  consensus::Round round{kHeight, 1};

  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({commit_message}), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillOnce(Return(rxcpp::observable<>::just(commit_message)));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([this, round](auto commit_event) {
    EXPECT_EQ(this->ledger_peers, commit_event.ledger_state->ledger_peers);
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
    ASSERT_EQ(commit_event.round, round);
  });

  gate_outcome.get_subscriber().on_next(
      consensus::VoteOther(round, ledger_state, public_keys, hash));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given A commit from consensus and initialized components
 * @when gate have voted for other block and multiple blocks are loaded
 * @then Successful commit
 */
TEST_F(SynchronizerTest, ValidWhenValidChainMultipleBlocks) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);

  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  const auto target_height = kHeight + 1;
  auto target_commit = makeCommit(target_height);
  EXPECT_CALL(*mutable_factory, commit_(_))
      .WillOnce(Return(ByMove(expected::makeValue(std::make_shared<LedgerState>(
          ledger_peers, target_height, target_commit->hash())))));
  std::vector<std::shared_ptr<shared_model::interface::Block>> commits{
      commit_message, target_commit};
  EXPECT_CALL(*chain_validator, validateAndApply(ChainEq(commits), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillOnce(Return(rxcpp::observable<>::iterate(commits)));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([this, target_height](auto commit_event) {
    EXPECT_EQ(this->ledger_peers, commit_event.ledger_state->ledger_peers);
    ASSERT_EQ(commit_event.round.block_round, target_height);
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
  });

  gate_outcome.get_subscriber().on_next(consensus::VoteOther(
      consensus::Round{kHeight, 1}, ledger_state, public_keys, hash));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given A commit from consensus and initialized components
 * @when gate have voted for other block
 * @then retrieveBlocks called again after unsuccessful download attempt
 */
TEST_F(SynchronizerTest, ExactlyThreeRetrievals) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);
  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);
  {
    InSequence s;  // ensures the call order
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({}), _))
        .WillOnce(Return(true));
    EXPECT_CALL(*chain_validator,
                validateAndApply(ChainEq({commit_message}), _))
        .WillOnce(Return(false));
    EXPECT_CALL(*chain_validator,
                validateAndApply(ChainEq({commit_message}), _))
        .WillOnce(Return(true));
  }
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillOnce(Return(rxcpp::observable<>::empty<
                       std::shared_ptr<shared_model::interface::Block>>()))
      .WillOnce(Return(rxcpp::observable<>::just(commit_message)))
      .WillOnce(Return(rxcpp::observable<>::just(commit_message)));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe();

  gate_outcome.get_subscriber().on_next(consensus::VoteOther(
      consensus::Round{kHeight, 1}, ledger_state, public_keys, hash));

  ASSERT_TRUE(wrapper.validate());
}

MATCHER_P(StringEqSharedPtr, ptr, "equals " + *ptr) {
  return std::string_view{arg} == std::string_view{*ptr};
}

template <typename Strong>
struct StringViewHelper {
  std::shared_ptr<std::string> holder{std::make_shared<std::string>()};

  StringViewHelper<Strong> &operator=(std::string_view s) {
    *holder = s;
    return *this;
  }

  operator testing::Matcher<Strong>() const {
    return StringEqSharedPtr(holder);
  }
};

/**
 * @given A commit from consensus and initialized components. First peer that we
 * request blocks from provides a bad block in the middle of the block chain.
 * @when gate has voted for other block in the future
 * @then retrieveBlocks called again with another peer after failure in block
 * chain middle
 */
TEST_F(SynchronizerTest, FailureInMiddleOfChainThenSuccessWithOtherPeer) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);
  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  const size_t kConsensusHeight = kInitTopBlockHeight + 10;
  const size_t kBadBlockNumber = 5;  // in the middle
  const size_t kBadBlockHeight =
      kInitTopBlockHeight + kBadBlockNumber;  // in the middle
  std::vector<std::shared_ptr<shared_model::interface::Block>> chain_bad;
  std::vector<std::shared_ptr<shared_model::interface::Block>> chain_good;

  for (auto height = kInitTopBlockHeight + 1; height <= kConsensusHeight;
       ++height) {
    chain_bad.emplace_back(makeCommit(height));
  }
  for (auto height = kBadBlockHeight; height <= kConsensusHeight; ++height) {
    chain_good.emplace_back(makeCommit(height));
  }

  StringViewHelper<PublicKeyHexStringView> first_asked_peer;
  {
    using namespace testing;

    InSequence s;  // ensures the call order

    // first attempt: get blocks till kBadBlockHeight, then fail
    EXPECT_CALL(*block_loader, retrieveBlocks(kInitTopBlockHeight, _))
        .WillOnce(DoAll(SaveArg<1>(&first_asked_peer),
                        Return(rxcpp::observable<>::iterate(chain_bad))));
    EXPECT_CALL(*chain_validator, validateAndApply(_, _))
        .WillOnce([&](auto chain, auto const &) {
          chain.take(kBadBlockNumber).as_blocking().last();
          return false;
        });

    // second attempt: request blocks from kBadBlockHeight and commit
    const auto kRetrieveBlocksArg =
        kBadBlockHeight - 1;  // for whatever reason, to request blocks starting
                              // with N, we need to pass N-1...
    EXPECT_CALL(*block_loader,
                retrieveBlocks(kRetrieveBlocksArg, Not(first_asked_peer)))
        .WillOnce(Return(rxcpp::observable<>::iterate(chain_good)));
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq(chain_good), _))
        .WillOnce(Return(true));
  }

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe();

  gate_outcome.get_subscriber().on_next(consensus::Future{
      consensus::Round{kConsensusHeight, 1}, ledger_state, public_keys});

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given A commit from consensus and initialized components. First peer that we
 * request blocks from is slow and provides only some part of the block chain.
 * @when gate has voted for other block in the future
 * @then retrieveBlocks called again on other peer after partial syncing with
 * the slow peer
 */
TEST_F(SynchronizerTest, SyncTillMiddleOfChainThenSuccessWithOtherPeer) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);
  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  const size_t kConsensusHeight = kInitTopBlockHeight + 10;
  const size_t kBlocksFrom1stPeer = 5;  // in the middle
  const size_t k1stPeerHeight =
      kInitTopBlockHeight + kBlocksFrom1stPeer;  // in the middle
  std::vector<std::shared_ptr<shared_model::interface::Block>> chain_1st_peer;
  std::vector<std::shared_ptr<shared_model::interface::Block>> chain_2nd_peer;

  for (auto height = kInitTopBlockHeight + 1; height <= k1stPeerHeight;
       ++height) {
    chain_1st_peer.emplace_back(makeCommit(height));
  }
  for (auto height = k1stPeerHeight; height <= kConsensusHeight; ++height) {
    chain_2nd_peer.emplace_back(makeCommit(height));
  }

  StringViewHelper<PublicKeyHexStringView> first_asked_peer;
  {
    using namespace testing;

    InSequence s;  // ensures the call order

    // first attempt: get some blocks till k1stPeerHeight
    EXPECT_CALL(*block_loader, retrieveBlocks(kInitTopBlockHeight, _))
        .WillOnce(DoAll(SaveArg<1>(&first_asked_peer),
                        Return(rxcpp::observable<>::iterate(chain_1st_peer))));
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq(chain_1st_peer), _))
        .WillOnce(Return(true));

    // then try again with same peer but he has no more blocks
    const auto kRetrieveBlocksArg =
        k1stPeerHeight  // it is our height after 1st attempt
        + 1             // we want the next block
        - 1;            // but for whatever reason, to request blocks starting
                        // with N, we need to pass N-1...
    EXPECT_CALL(*block_loader,
                retrieveBlocks(kRetrieveBlocksArg, first_asked_peer))
        .WillRepeatedly(
            Return(rxcpp::observable<>::empty<
                   std::shared_ptr<shared_model::interface::Block>>()));
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({}), _))
        .WillOnce(Return(true));

    // then request blocks from second peer starting from k1stPeerHeight
    EXPECT_CALL(*block_loader,
                retrieveBlocks(kRetrieveBlocksArg, Not(first_asked_peer)))
        .WillOnce(Return(rxcpp::observable<>::iterate(chain_2nd_peer)));
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq(chain_2nd_peer), _))
        .WillOnce(Return(true));
  }

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe();

  gate_outcome.get_subscriber().on_next(consensus::Future{
      consensus::Round{kConsensusHeight, 1}, ledger_state, public_keys});

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given A commit from consensus and initialized components
 * @when gate has voted for other block in the future. block loading abrupts in
 * the middile.
 * @then retrieveBlocks called again on same peer after connection abruption and
 * sync completes
 */
TEST_F(SynchronizerTest, AbruptInMiddleOfChainThenSuccessWithSamePeer) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);
  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  const size_t kConsensusHeight = kInitTopBlockHeight + 10;
  const size_t kBlocksIn1stTry = 5;  // in the middle
  const size_t kAbruptHeight =
      kInitTopBlockHeight + kBlocksIn1stTry;  // in the middle
  std::vector<std::shared_ptr<shared_model::interface::Block>> chain_1st_try;
  std::vector<std::shared_ptr<shared_model::interface::Block>> chain_2nd_try;

  for (auto height = kInitTopBlockHeight + 1; height <= kAbruptHeight;
       ++height) {
    chain_1st_try.emplace_back(makeCommit(height));
  }
  for (auto height = kAbruptHeight; height <= kConsensusHeight; ++height) {
    chain_2nd_try.emplace_back(makeCommit(height));
  }

  StringViewHelper<PublicKeyHexStringView> first_asked_peer;
  {
    using namespace testing;

    InSequence s;  // ensures the call order

    // first attempt: get blocks till kAbruptHeight
    EXPECT_CALL(*block_loader, retrieveBlocks(kInitTopBlockHeight, _))
        .WillOnce(DoAll(SaveArg<1>(&first_asked_peer),
                        Return(rxcpp::observable<>::iterate(chain_1st_try))));
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq(chain_1st_try), _))
        .WillOnce(Return(true));

    // second attempt: request blocks from same peer starting at kAbruptHeight
    const auto kRetrieveBlocksArg =
        kAbruptHeight  // it is our height after 1st attempt
        + 1            // we want the next block
        - 1;           // but for whatever reason, to request blocks starting
                       // with N, we need to pass N-1...
    EXPECT_CALL(*block_loader,
                retrieveBlocks(kRetrieveBlocksArg, first_asked_peer))
        .WillOnce(Return(rxcpp::observable<>::iterate(chain_2nd_try)));
    EXPECT_CALL(*chain_validator, validateAndApply(ChainEq(chain_2nd_try), _))
        .WillOnce(Return(true));
  }

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe();

  gate_outcome.get_subscriber().on_next(consensus::Future{
      consensus::Round{kConsensusHeight, 1}, ledger_state, public_keys});

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given commit from the consensus and initialized components
 * @when synchronizer fails to download blocks from all the peers in the list
 * @then no commit event is emitted
 */
TEST_F(SynchronizerTest, RetrieveBlockSeveralFailures) {
  const size_t number_of_failures{ledger_peers.size()};
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);
  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillRepeatedly(Return(rxcpp::observable<>::just(commit_message)));

  EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({commit_message}), _))
      .Times(number_of_failures)
      .WillRepeatedly(Return(false));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 0);
  wrapper.subscribe();

  gate_outcome.get_subscriber().on_next(consensus::VoteOther(
      consensus::Round{kHeight, 1}, ledger_state, public_keys, hash));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given initialized components
 * @when gate have got reject on proposal
 * @then synchronizer output is also reject
 */
TEST_F(SynchronizerTest, ProposalRejectOutcome) {
  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([](auto commit_event) {
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kReject);
  });

  mutableStorageExpectChain(*mutable_factory, {});
  EXPECT_CALL(*chain_validator, validateAndApply(_, _)).Times(0);

  gate_outcome.get_subscriber().on_next(consensus::ProposalReject(
      consensus::Round{kHeight, 1}, ledger_state, public_keys));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given initialized components
 * @when gate have got reject on block
 * @then synchronizer output is also reject
 */
TEST_F(SynchronizerTest, BlockRejectOutcome) {
  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([](auto commit_event) {
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kReject);
  });

  mutableStorageExpectChain(*mutable_factory, {});
  EXPECT_CALL(*chain_validator, validateAndApply(_, _)).Times(0);

  gate_outcome.get_subscriber().on_next(consensus::BlockReject(
      consensus::Round{kHeight, 1}, ledger_state, public_keys));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given initialized components
 * @when gate have got agreement on none
 * @then synchronizer output is also none
 */
TEST_F(SynchronizerTest, NoneOutcome) {
  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([](auto commit_event) {
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kNothing);
  });

  mutableStorageExpectChain(*mutable_factory, {});
  EXPECT_CALL(*chain_validator, validateAndApply(_, _)).Times(0);

  gate_outcome.get_subscriber().on_next(consensus::AgreementOnNone(
      consensus::Round{kHeight, 1}, ledger_state, public_keys));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given commit with the block peer voted for
 * @when synchronizer processes the commit
 * @then commitPrepared is called @and commit is not called
 */
TEST_F(SynchronizerTest, VotedForBlockCommitPrepared) {
  EXPECT_CALL(*mutable_factory, preparedCommitEnabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mutable_factory, commitPrepared(_))
      .WillOnce(Return(
          ByMove(CommitResult{expected::makeValue(std::make_shared<LedgerState>(
              ledger_peers, kHeight, commit_message->hash()))})));

  EXPECT_CALL(*mutable_factory, commit_(_)).Times(0);

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([this](auto commit_event) {
    EXPECT_EQ(this->ledger_peers, commit_event.ledger_state->ledger_peers);
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
  });

  mutableStorageExpectChain(*mutable_factory, {});

  gate_outcome.get_subscriber().on_next(consensus::PairValid(
      consensus::Round{kHeight, 1}, ledger_state, commit_message));
}

/**
 * @given commit with the block which is different than the peer has voted for
 * @when synchronizer processes the commit
 * @then commitPrepared is not called @and commit is called
 */
TEST_F(SynchronizerTest, VotedForOtherCommitPrepared) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);

  EXPECT_CALL(*mutable_factory, preparedCommitEnabled()).Times(0);
  EXPECT_CALL(*mutable_factory, commitPrepared(_)).Times(0);

  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillRepeatedly(Return(rxcpp::observable<>::just(commit_message)));

  EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({commit_message}), _))
      .WillOnce(Return(true));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([](auto commit_event) {
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
  });

  gate_outcome.get_subscriber().on_next(consensus::VoteOther(
      consensus::Round{kHeight, 1}, ledger_state, public_keys, hash));
}

/**
 * @given commit with the block peer voted for
 * @when synchronizer processes the commit @and commit prepared is unsuccessful
 * @then commit is called and synchronizer works as expected
 */
TEST_F(SynchronizerTest, VotedForThisCommitPreparedFailure) {
  EXPECT_CALL(*mutable_factory, preparedCommitEnabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mutable_factory, commitPrepared(_)).Times(0);

  mutableStorageExpectChain(*mutable_factory, {commit_message});

  EXPECT_CALL(*mutable_factory, commit_(_))
      .WillOnce(Return(ByMove(expected::makeValue(
          std::make_shared<LedgerState>(ledger_peers, kHeight, hash)))));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([](auto commit_event) {
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
  });

  gate_outcome.get_subscriber().on_next(consensus::PairValid(
      consensus::Round{kHeight, 1}, ledger_state, commit_message));
}

/**
 * @given A commit from consensus and initialized components
 * @when a valid block that can be applied and commit fails
 * @then no commit event is emitted
 */
TEST_F(SynchronizerTest, CommitFailureVoteSameBlock) {
  EXPECT_CALL(*mutable_factory, preparedCommitEnabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mutable_factory, commitPrepared(_)).Times(0);
  mutableStorageExpectChain(*mutable_factory, {commit_message});
  EXPECT_CALL(*mutable_factory, commit_(_))
      .WillOnce(Return(ByMove(expected::makeError(""))));
  EXPECT_CALL(*chain_validator, validateAndApply(_, _)).Times(0);
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _)).Times(0);

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 0);

  gate_outcome.get_subscriber().on_next(consensus::PairValid(
      consensus::Round{kHeight, 1}, ledger_state, commit_message));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given A commit from consensus and initialized components
 * @when gate has voted for other block and commit fails
 * @then no commit event is emitted
 */
TEST_F(SynchronizerTest, CommitFailureVoteOther) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);

  mutableStorageExpectChain(*mutable_factory, {});

  EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({commit_message}), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillOnce(Return(rxcpp::observable<>::just(commit_message)));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 0);

  gate_outcome.get_subscriber().on_next(consensus::VoteOther(
      consensus::Round{kHeight, 1}, ledger_state, public_keys, hash));

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given Peers top block height is kHeight - 1
 * @when arrives Future with kHeight + 1 round
 * @then synchronizer has to download missing block with height = kHeight
 */
TEST_F(SynchronizerTest, OneRoundDifference) {
  DefaultValue<expected::Result<std::unique_ptr<MutableStorage>, std::string>>::
      SetFactory(&createMockMutableStorage);

  EXPECT_CALL(*mutable_factory, createMutableStorage(_)).Times(1);

  EXPECT_CALL(*chain_validator, validateAndApply(ChainEq({commit_message}), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*block_loader, retrieveBlocks(_, _))
      .WillOnce(Return(rxcpp::observable<>::just(commit_message)));

  auto wrapper =
      make_test_subscriber<CallExact>(synchronizer->on_commit_chain(), 1);
  wrapper.subscribe([this,
                     expected_round = consensus::Round{commit_message->height(),
                                                       0}](auto commit_event) {
    EXPECT_EQ(this->ledger_peers, commit_event.ledger_state->ledger_peers);
    ASSERT_EQ(commit_event.sync_outcome, SynchronizationOutcomeType::kCommit);
    ASSERT_EQ(commit_event.round, expected_round);
  });

  gate_outcome.get_subscriber().on_next(consensus::Future(
      consensus::Round{kHeight + 1, 1}, ledger_state, public_keys));

  ASSERT_TRUE(wrapper.validate());
}
