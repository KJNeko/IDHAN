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

	//! Keyed by (left << 32 | right).
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
	[[nodiscard]] std::vector< std::int64_t > encode( std::string_view text ) const;

	//! The token strings \p text encodes to, for the tokenization diagnostic.
	[[nodiscard]] std::vector< std::string > encodePieces( std::string_view text ) const;
};

} // namespace premade
