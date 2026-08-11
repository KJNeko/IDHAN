//
// Created by kj16609 on 8/11/26.
//
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace premade
{

//! A byte-pair encoder driven entirely by a HuggingFace `tokenizer.json`.
/** Written rather than vendored because every off-the-shelf option fails the setup constraint: the
 *  onnxruntime-extensions tokenizer ops ship only inside a pip wheel, `tokenizers-cpp` puts a Rust
 *  toolchain in the build, and sentencepiece is not packaged on the target distribution. Reading the
 *  clone's own tokenizer.json needs nothing installed at all.
 *
 *  Every rule below was read out of `onnx-community/siglip2-base-patch16-224-ONNX/tokenizer.json` and
 *  checked against the HuggingFace `tokenizers` library, not inferred from documentation:
 *
 *  - The normalizer replaces U+0020 with U+2581 and does **nothing else**. In particular it does not
 *    lowercase and does not strip punctuation, so `CatGirl` and `catgirl` tokenize differently and
 *    `rating:safe` keeps its colon. `tokenizer_config.json` claims `do_lower_case: true`, but that is
 *    a slow-tokenizer attribute the fast path ignores -- believing it would silently mis-tokenize
 *    every capitalised query.
 *  - The pre-tokenizer splits on U+0020, which after normalization is a no-op: no spaces remain, so
 *    the whole phrase is one BPE word.
 *  - `byte_fallback` is on. A character absent from the vocabulary becomes one `<0xNN>` token per
 *    UTF-8 byte rather than a single unk.
 *  - The post-processor appends `<eos>` and no `<bos>`.
 *  - Padding is right, to a fixed length, with `<pad>`.
 *
 *  A tokenizer that is subtly wrong produces plausible vectors and no error anywhere, which is why
 *  none of this is guessed and why `encodePieces` exists to make the result inspectable. */
class BpeTokenizer
{
	//! What a merge rule produces, and how early it applies.
	struct Merge
	{
		std::int32_t m_rank { 0 }; //!< Lower wins. Merge order is the whole of BPE's behaviour.
		std::int32_t m_result { 0 }; //!< Vocabulary id of the concatenation.
	};

	std::unordered_map< std::string, std::int32_t > m_vocab {};
	//! Reverse lookup, for the tokenization diagnostic. Ids are dense, so a vector suffices.
	std::vector< std::string > m_tokens {};

	//! Keyed by (left << 32 | right). Pairs of ids rather than of strings: the string form costs
	//! roughly 60 MB across 580k rules and a hash of two allocations per lookup.
	std::unordered_map< std::uint64_t, Merge > m_merges {};

	//! Byte value to `<0xNN>` id, so the fallback path is an index rather than a formatted lookup.
	std::array< std::int32_t, 256 > m_byte_tokens {};

	bool m_byte_fallback { true };
	std::int32_t m_unk_id { -1 };
	std::int32_t m_eos_id { -1 };
	std::int32_t m_pad_id { 0 };
	std::size_t m_context_length { 64 };

	//! Runs the merge rules over one already-normalised phrase.
	[[nodiscard]] std::vector< std::int32_t > applyMerges( std::string_view normalised ) const;

  public:

	BpeTokenizer() = default;

	//! Loads a `tokenizer.json`.
	/** \param context_length What the text graph's input axis declares. The file's own padding
	 *         strategy is used when it names one and this is zero. */
	[[nodiscard]] static std::expected< BpeTokenizer, std::string > load(
		const std::filesystem::path& tokenizer_path,
		std::size_t context_length );

	[[nodiscard]] std::size_t contextLength() const { return m_context_length; }

	//! Encodes \p text into exactly contextLength() ids: the phrase, `<eos>`, then `<pad>`.
	/** A phrase longer than the context is truncated to leave room for `<eos>`, which is kept. The
	 *  graph's input axis is fixed, so there is no option to return more, and dropping the terminator
	 *  instead would hand the model a sequence it never saw in training. */
	[[nodiscard]] std::vector< std::int64_t > encode( std::string_view text ) const;

	//! The token strings \p text encodes to, for the tokenization diagnostic.
	/** Padding is omitted -- forty `<pad>` entries tell a human nothing about whether tokenization
	 *  was right. */
	[[nodiscard]] std::vector< std::string > encodePieces( std::string_view text ) const;
};

} // namespace premade
