/*
 * Copyright (c) 2024, stelar7 <dudedbz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ByteReader.h>
#include <AK/Endian.h>
#include <AK/Function.h>
#include <AK/Random.h>
#include <LibCrypto/BigInt/UnsignedBigInteger.h>

namespace Crypto::Padding {

// https://datatracker.ietf.org/doc/html/rfc2437#section-9.1.1
class OAEP {
public:
    // https://datatracker.ietf.org/doc/html/rfc2437#section-9.1.1.1
    template<typename HashFunction, typename MaskGenerationFunction>
    static ErrorOr<ByteBuffer> encode(ReadonlyBytes message, ReadonlyBytes parameters, size_t length, Function<void(Bytes)> seed_function = fill_with_random)
    {
        // FIXME: 1. If the length of P is greater than the input limitation for the
        // hash function (2^61-1 octets for SHA-1) then output "parameter string
        // too long" and stop.

        // 2. If ||M|| > emLen - 2hLen - 1 then output "message too long" and stop.
        auto h_len = HashFunction::digest_size();
        auto max_message_size = length - (2 * h_len) - 1;
        if (message.size() > max_message_size)
            return Error::from_string_literal("message too long");

        // 3. Generate an octet string PS consisting of emLen-||M||-2hLen-1 zero octets. The length of PS may be 0.
        auto padding_size = length - message.size() - (2 * h_len) - 1;
        auto ps = TRY(ByteBuffer::create_zeroed(padding_size));

        // 4. Let pHash = Hash(P), an octet string of length hLen.
        HashFunction hash;
        hash.update(parameters);
        auto digest = hash.digest();
        auto p_hash = digest.bytes();

        // 5. Concatenate pHash, PS, the message M, and other padding to form a data block DB as: DB = pHash || PS || 01 || M
        auto db = TRY(ByteBuffer::create_uninitialized(0));
        TRY(db.try_append(p_hash));
        TRY(db.try_append(ps.bytes()));
        TRY(db.try_append(0x01));
        TRY(db.try_append(message));

        // 6. Generate a random octet string seed of length hLen.
        auto seed = TRY(ByteBuffer::create_uninitialized(h_len));
        seed_function(seed);

        // 7. Let dbMask = MGF(seed, emLen-hLen).
        auto db_mask = TRY(MaskGenerationFunction::template mgf1<HashFunction>(seed, length - h_len));

        // 8. Let maskedDB = DB \xor dbMask.
        auto masked_db = TRY(ByteBuffer::xor_buffers(db, db_mask));

        // 9. Let seedMask = MGF(maskedDB, hLen).
        auto seed_mask = TRY(MaskGenerationFunction::template mgf1<HashFunction>(masked_db, h_len));

        // 10. Let maskedSeed = seed \xor seedMask.
        auto masked_seed = TRY(ByteBuffer::xor_buffers(seed, seed_mask));

        // 11. Let EM = maskedSeed || maskedDB.
        auto em = TRY(ByteBuffer::create_uninitialized(0));
        TRY(em.try_append(masked_seed));
        TRY(em.try_append(masked_db));

        // 12. Output EM.
        return em;
    }

    // https://www.rfc-editor.org/rfc/rfc3447#section-7.1.1
    template<typename HashFunction, typename MaskGenerationFunction>
    static ErrorOr<ByteBuffer> eme_encode(ReadonlyBytes message, ReadonlyBytes label, u32 rsa_modulus_n, Function<void(Bytes)> seed_function = fill_with_random)
    {
        // FIXME: 1. If the length of L is greater than the input limitation for the
        //           hash function (2^61 - 1 octets for SHA-1), output "label too
        //           long" and stop.

        // 2. If mLen > k - 2hLen - 2, output "message too long" and stop.
        auto m_len = message.size();
        auto k = rsa_modulus_n;
        auto h_len = HashFunction::digest_size();
        auto max_message_size = k - (2 * h_len) - 2;
        if (m_len > max_message_size)
            return Error::from_string_view("message too long"sv);

        // 3. If the label L is not provided, let L be the empty string. Let lHash = Hash(L), an octet string of length hLen.
        HashFunction hash;
        hash.update(label);
        auto digest = hash.digest();
        auto l_hash = digest.bytes();

        // 4. Generate an octet string PS consisting of k - mLen - 2hLen - 2 zero octets.  The length of PS may be zero.
        auto ps_size = k - m_len - (2 * h_len) - 2;
        auto ps = TRY(ByteBuffer::create_zeroed(ps_size));

        // 5. Concatenate lHash, PS, a single octet with hexadecimal value 0x01, and the message M
        //    to form a data block DB of length k - hLen - 1 octets as DB = lHash || PS || 0x01 || M.
        auto db = TRY(ByteBuffer::create_uninitialized(0));
        TRY(db.try_append(l_hash));
        TRY(db.try_append(ps.bytes()));
        TRY(db.try_append(0x01));
        TRY(db.try_append(message));

        // 6. Generate a random octet string seed of length hLen.
        auto seed = TRY(ByteBuffer::create_uninitialized(h_len));
        seed_function(seed);

        // 7. Let dbMask = MGF(seed, k - hLen - 1).
        auto db_mask = TRY(MaskGenerationFunction::template mgf1<HashFunction>(seed, k - h_len - 1));

        // 8. Let maskedDB = DB \xor dbMask.
        auto masked_db = TRY(ByteBuffer::xor_buffers(db, db_mask));

        // 9. Let seedMask = MGF(maskedDB, hLen).
        auto seed_mask = TRY(MaskGenerationFunction::template mgf1<HashFunction>(masked_db, h_len));

        // 10. Let maskedSeed = seed \xor seedMask.
        auto masked_seed = TRY(ByteBuffer::xor_buffers(seed, seed_mask));

        // 11. Concatenate a single octet with hexadecimal value 0x00, maskedSeed, and maskedDB
        //     to form an encoded message EM of length k octets as EM = 0x00 || maskedSeed || maskedDB.
        auto em = TRY(ByteBuffer::create_uninitialized(0));
        TRY(em.try_append(0x00));
        TRY(em.try_append(masked_seed));
        TRY(em.try_append(masked_db));

        // 12. Output EM.
        return em;
    }
};

}
