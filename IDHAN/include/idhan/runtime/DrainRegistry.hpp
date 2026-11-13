#pragma once
#include <cstddef>
#include <memory>
#include <optional>

#include "idhan/runtime/Task.hpp"

namespace idhan::runtime
{
class DrainRegistry;

class WorkRegistration
{
	struct State;
	std::shared_ptr< State > m_state;
	friend class DrainRegistry;
	explicit WorkRegistration( std::shared_ptr< State > );

  public:

	WorkRegistration( const WorkRegistration& ) = delete;
	WorkRegistration& operator=( const WorkRegistration& ) = delete;
	WorkRegistration( WorkRegistration&& ) noexcept;
	WorkRegistration& operator=( WorkRegistration&& ) noexcept;
	~WorkRegistration();
	[[nodiscard]] WorkRegistration child() const;
};

class DrainRegistry
{
	std::shared_ptr< WorkRegistration::State > m_state;

  public:

	DrainRegistry();
	~DrainRegistry();
	DrainRegistry( const DrainRegistry& ) = delete;
	DrainRegistry& operator=( const DrainRegistry& ) = delete;
	[[nodiscard]] std::optional< WorkRegistration > admit();
	void closeAdmission();
	[[nodiscard]] std::size_t outstanding() const;
	[[nodiscard]] Task<> drain();
};

} // namespace idhan::runtime
