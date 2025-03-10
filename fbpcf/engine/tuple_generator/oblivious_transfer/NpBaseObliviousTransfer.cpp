/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fbpcf/engine/tuple_generator/oblivious_transfer/NpBaseObliviousTransfer.h"
#include <emmintrin.h>
#include <openssl/sha.h>
#include <stdexcept>

namespace fbpcf::engine::tuple_generator::oblivious_transfer {

NpBaseObliviousTransfer::NpBaseObliviousTransfer(
    std::unique_ptr<communication::IPartyCommunicationAgent> agent)
    : agent_{std::move(agent)} {
  group_ = std::unique_ptr<EC_GROUP, std::function<void(EC_GROUP*)>>(
      EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1), EC_GROUP_clear_free);
  if (group_ == nullptr) {
    throw std::runtime_error("Failed to create group.");
  }
  order_ =
      std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>>(BN_new(), BN_free);

  // Create a CTX variable. CTX variables are used as temporary variable for
  // many Openssl functions.
  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);

  if (order_ == nullptr || ctx == nullptr) {
    throw std::runtime_error("Failed to initialize.");
  }

  if (EC_GROUP_get_order(group_.get(), order_.get(), ctx.get()) != 1) {
    throw std::runtime_error("Failed to get group order.");
  }
}

void NpBaseObliviousTransfer::sendPoint(const EC_POINT& point) const {
  // Create a CTX variable. CTX variables are used as temporary variable for
  // many Openssl functions.
  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);

  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create BN_CTX.");
  }

  std::unique_ptr<char, std::function<void(char*)>> buf(
      EC_POINT_point2hex(
          group_.get(), &point, POINT_CONVERSION_COMPRESSED, ctx.get()),
      free);

  if (buf == nullptr) {
    throw std::runtime_error("Failed to convert point to hex.");
  }

  size_t size = strlen(buf.get());
  if (size == 0) {
    throw std::runtime_error("Failed to convert point to hex.");
  }
  agent_->sendSingleT<size_t>(size);
  std::vector<unsigned char> tmp(buf.get(), buf.get() + size);
  agent_->send(tmp);
}

NpBaseObliviousTransfer::PointPointer NpBaseObliviousTransfer::receivePoint()
    const {
  PointPointer rst(EC_POINT_new(group_.get()), EC_POINT_free);

  if (rst == nullptr) {
    throw std::runtime_error("Failed to create new point.");
  }

  size_t size = agent_->receiveSingleT<size_t>();
  auto tmp = agent_->receive(size);
  // Create a CTX variable. CTX variables are used as temporary variable for
  // many Openssl functions.
  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);

  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create BN_CTX.");
  }

  if (EC_POINT_hex2point(
          group_.get(),
          reinterpret_cast<const char*>(tmp.data()),
          rst.get(),
          ctx.get()) == nullptr) {
    throw std::runtime_error("Failed to convert hex to point.");
  }
  return rst;
}

NpBaseObliviousTransfer::PointPointer
NpBaseObliviousTransfer::generateRandomPoint() const {
  // Create a CTX variable. CTX variables are used as temporary variable for
  // many Openssl functions.
  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);

  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create BN_CTX.");
  }

  std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>> randomBn(
      BN_new(), BN_free);

  if (randomBn == nullptr) {
    throw std::runtime_error("Failed to create new big number.");
  }

  if (BN_rand_range(randomBn.get(), order_.get()) != 1) {
    throw std::runtime_error("Failed to generate a random big number.");
  }
  PointPointer randomPoint(EC_POINT_new(group_.get()), EC_POINT_free);

  if (randomPoint == nullptr) {
    throw std::runtime_error("Failed to create new point.");
  }

  // EC_POINT_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *n,
  // const EC_POINT *q, const BIGNUM *m, BN_CTX *ctx) calculates the value
  // generator * n + q * m and stores the result in r. The value n may be NULL
  // in which case the result is just q * m (variable point multiplication).
  // Alternatively, both q and m may be NULL, and n non-NULL, in which case the
  // result is just generator * n (fixed point multiplication).

  if (EC_POINT_mul(
          group_.get(),
          randomPoint.get(),
          randomBn.get(),
          nullptr,
          nullptr,
          ctx.get()) != 1) {
    throw std::runtime_error("Failed to generate a random point.");
  };

  return randomPoint;
}

__m128i NpBaseObliviousTransfer::hashPoint(
    const EC_POINT& point,
    uint64_t nonce) const {
  std::vector<unsigned char> digest(SHA256_DIGEST_LENGTH);

  // Create a CTX variable. CTX variables are used as temporary variable for
  // many Openssl functions.
  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);

  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create BN_CTX.");
  }

  std::unique_ptr<char, std::function<void(char*)>> buf(
      EC_POINT_point2hex(
          group_.get(), &point, POINT_CONVERSION_COMPRESSED, ctx.get()),
      free);

  if (buf == nullptr) {
    throw std::runtime_error("Failed to covert point to hex values.");
  }

  SHA256_CTX shaCtx;

  if (SHA256_Init(&shaCtx) != 1) {
    throw std::runtime_error("Failed to init SHA256.");
  }
  if (SHA256_Update(&shaCtx, buf.get(), strlen(buf.get())) != 1) {
    throw std::runtime_error("Failed to update SHA256 with point hex.");
  }
  if (SHA256_Update(&shaCtx, &nonce, sizeof(uint64_t)) != 1) {
    throw std::runtime_error("Failed to update SHA256 with nonce.");
  }
  if (SHA256_Final(digest.data(), &shaCtx) != 1) {
    throw std::runtime_error("Failed to finalize SHA256.");
  }

  return _mm_set_epi8(
      digest.at(0),
      digest.at(1),
      digest.at(2),
      digest.at(3),
      digest.at(4),
      digest.at(5),
      digest.at(6),
      digest.at(7),
      digest.at(8),
      digest.at(9),
      digest.at(10),
      digest.at(11),
      digest.at(12),
      digest.at(13),
      digest.at(14),
      digest.at(15));
}

std::pair<std::vector<__m128i>, std::vector<__m128i>>
NpBaseObliviousTransfer::send(size_t size) {
  // This global M is only used for all the OT instances in this function.
  // Obviously this batch of OTs are between the same pair of parties.
  auto globalM = generateRandomPoint();
  sendPoint(*globalM);

  // a vector of random big numbers
  std::vector<std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>>> randomRs(
      size);

  // g^r
  std::vector<PointPointer> gr(size);

  // M^r
  std::vector<PointPointer> mr(size);

  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);
  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create BN_CTX.");
  }

  {
    for (size_t i = 0; i < size; i++) {
      // set randomRs[i] to be a random big number
      randomRs[i] = std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>>(
          BN_new(), BN_free);
      if (randomRs.at(i) == nullptr) {
        throw std::runtime_error("Failed to create big number.");
      }

      if (BN_rand_range(randomRs[i].get(), order_.get()) != 1) {
        throw std::runtime_error("Failed to generate randomRs[i].");
      }

      // initialize gr[i].
      gr[i] = PointPointer(EC_POINT_new(group_.get()), EC_POINT_free);
      if (gr.at(i) == nullptr) {
        throw std::runtime_error("Failed to create point.");
      }

      // set gr[i] to be g^r[i]
      if (EC_POINT_mul(
              group_.get(),
              gr[i].get(),
              randomRs.at(i).get(),
              nullptr,
              nullptr,
              ctx.get()) != 1) {
        throw std::runtime_error("Failed to compute gr[i].");
      };

      // initialize mr[i].
      mr[i] = PointPointer(EC_POINT_new(group_.get()), EC_POINT_free);

      if (mr.at(i) == nullptr) {
        throw std::runtime_error("Failed to create point.");
      }

      // set mr[i] to be M^r[i]
      if (EC_POINT_mul(
              group_.get(),
              mr[i].get(),
              nullptr,
              globalM.get(),
              randomRs.at(i).get(),
              ctx.get()) != 1) {
        throw std::runtime_error("Failed to compute mr[i].");
      };
    }
  }

  // s
  std::vector<PointPointer> s(size);
  for (size_t i = 0; i < size; i++) {
    s[i] = receivePoint();
  }

  for (size_t i = 0; i < size; i++) {
    sendPoint(*gr.at(i));
  }

  // two vectors of EC points t0 and t1
  std::vector<std::vector<PointPointer>> t(2);
  t[0].reserve(size);
  t[1].reserve(size);

  PointPointer tmp(EC_POINT_new(group_.get()), EC_POINT_free);
  for (size_t i = 0; i < size; i++) {
    t[0].push_back(PointPointer(EC_POINT_new(group_.get()), EC_POINT_free));
    t[1].push_back(PointPointer(EC_POINT_new(group_.get()), EC_POINT_free));
    if (t.at(0).at(i) == nullptr) {
      throw std::runtime_error("Failed to create point.");
    }
    if (t.at(1).at(i) == nullptr) {
      throw std::runtime_error("Failed to create point.");
    }

    // set t[0][i] to be s[i]^r[i]
    if (EC_POINT_mul(
            group_.get(),
            t[0][i].get(),
            nullptr,
            s.at(i).get(),
            randomRs.at(i).get(),
            ctx.get()) != 1) {
      throw std::runtime_error("Failed to compute t[0][i].");
    };

    if (EC_POINT_copy(tmp.get(), t.at(0).at(i).get()) != 1) {
      throw std::runtime_error("Failed to copy t[0][i].");
    };

    if (EC_POINT_invert(group_.get(), tmp.get(), ctx.get()) != 1) {
      throw std::runtime_error("Failed to invert tmp.");
    }

    if (EC_POINT_add(
            group_.get(),
            t[1][i].get(),
            mr.at(i).get(),
            tmp.get(),
            ctx.get()) != 1) {
      throw std::runtime_error("Failed to compute t[1][i].");
    }
  }
  std::vector<__m128i> m0(size);
  std::vector<__m128i> m1(size);
  for (size_t i = 0; i < size; i++) {
    m0[i] = hashPoint(*t.at(0).at(i), 0);
    m1[i] = hashPoint(*t.at(1).at(i), 1);
  }
  return {std::move(m0), std::move(m1)};
}

std::vector<__m128i> NpBaseObliviousTransfer::receive(
    const std::vector<bool>& choice) {
  size_t size = choice.size();

  // a vector of random big numbers
  std::vector<std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>>> randomDs(
      size);

  // Create a CTX variable. CTX variables are used as temporary variable for
  // many Openssl functions.
  std::unique_ptr<BN_CTX, std::function<void(BN_CTX*)>> ctx(
      BN_CTX_new(), BN_CTX_free);
  if (ctx == nullptr) {
    throw std::runtime_error("Failed to create BN_CTX.");
  }

  // calculate the message for the sender; put these code in a scope such that
  // variables will expire and release the memory when they become irrevelant.
  {
    auto globalM = receivePoint();

    // two vectors of EC points s0 and s1
    std::vector<std::vector<PointPointer>> s(2);
    s[0].reserve(size);
    s[1].reserve(size);

    PointPointer tmp(EC_POINT_new(group_.get()), EC_POINT_free);

    if (tmp == nullptr) {
      throw std::runtime_error("Failed to create point.");
    }

    std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>> randomRange(
        BN_dup(order_.get()), BN_free);

    if (randomRange == nullptr) {
      throw std::runtime_error("Failed to create big number.");
    }

    if (BN_sub_word(randomRange.get(), 1) != 1) {
      throw std::runtime_error("Failed to calculate random range.");
    }

    for (size_t i = 0; i < size; i++) {
      // set randomDs[i] to be a random big number
      randomDs[i] = std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>>(
          BN_new(), BN_free);

      if (randomDs.at(i) == nullptr) {
        throw std::runtime_error("Failed to create big number.");
      }

      // we generate a random number in [0, q-2], then add 1 to it to get a
      // random number in [1, q-1].
      if (BN_rand_range(randomDs[i].get(), randomRange.get()) != 1) {
        throw std::runtime_error("Failed to generate randomDs[i].");
      }

      if (BN_add_word(randomDs[i].get(), 1) != 1) {
        throw std::runtime_error("Failed to correct randomDs[i].");
      }

      // initialize s[choice.at(i)][i].
      s[choice.at(i)].push_back(
          PointPointer(EC_POINT_new(group_.get()), EC_POINT_free));
      s[1 - choice.at(i)].push_back(nullptr);

      if (s.at(choice.at(i)).at(i) == nullptr) {
        throw std::runtime_error("Failed to create point.");
      }

      // set s[choice.at(i)][i] to be g^d[i]
      if (EC_POINT_mul(
              group_.get(),
              s[choice.at(i)][i].get(),
              randomDs.at(i).get(),
              nullptr,
              nullptr,
              ctx.get()) != 1) {
        throw std::runtime_error("Failed to compute s[choice.at(i)][i].");
      };

      // this block has no effects other than preventing timing attack if
      // choice.at(i) == 0.
      PointPointer newS0i(EC_POINT_new(group_.get()), EC_POINT_free);
      if (newS0i == nullptr) {
        throw std::runtime_error("Failed to create point.");
      }

      {
        if (EC_POINT_copy(tmp.get(), s.at(choice.at(i)).at(i).get()) != 1) {
          throw std::runtime_error("Failed to copy s[choice.at(i)][i].");
        }

        if (EC_POINT_invert(group_.get(), tmp.get(), ctx.get()) != 1) {
          throw std::runtime_error("Failed to invert tmp.");
        }

        if (EC_POINT_add(
                group_.get(),
                newS0i.get(),
                globalM.get(),
                tmp.get(),
                ctx.get()) != 1) {
          throw std::runtime_error("Failed to compute s[0][i].");
        }
      }

      // if choice.at(i) == 1, compute s[0][i] based on s[1][i].
      if (choice.at(i) == 1) {
        sendPoint(*newS0i);
      } else {
        sendPoint(*s.at(0).at(i));
      }
    }
  }

  // g
  std::vector<PointPointer> g;
  g.reserve(size);

  for (size_t i = 0; i < size; i++) {
    g.push_back(receivePoint());
  }

  // g^d
  std::vector<PointPointer> gd;
  gd.reserve(size);

  for (size_t i = 0; i < size; i++) {
    gd.push_back(PointPointer(EC_POINT_new(group_.get()), EC_POINT_free));
    if (gd.at(i) == nullptr) {
      throw std::runtime_error("Failed to create point.");
    }

    if (EC_POINT_mul(
            group_.get(),
            gd[i].get(),
            nullptr,
            g.at(i).get(),
            randomDs.at(i).get(),
            ctx.get()) != 1) {
      throw std::runtime_error("Failed to compute g^d.");
    };
  }

  std::vector<__m128i> m;
  m.reserve(size);
  for (size_t i = 0; i < size; i++) {
    m.push_back(hashPoint(*gd.at(i), choice.at(i)));
  }
  return m;
}

} // namespace fbpcf::engine::tuple_generator::oblivious_transfer
