//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_TAG_HPP
#define IDHAN_TAG_HPP

#include <string>






/*!
 * @page page-idhan-tags-info Tags
 * @subsection idhan-tags-info-relations Relations
 *	@subsubsection idhan-tags-info-parents Parents/Children
 * 		Parent/Children relationships can be used in order to provide a way to add well known tags automatically where it makes sense. A 'Parent' will always be added to the 'child' unless in cases where the Parent has a negative child present (Tags with a '-' appended to them)
 * 		* 'character:toujou koneko' is from the series 'Highschool DxD' thus 'series:Highschool DxD' can be added as a parent to 'character:toujou koneko'
 *
 * 		* 'Projekt Melody' is a vtuber from vshojo. The tag 'vtuber' and 'copyright:vshojo' can be added as a parent to 'character:projekt melody'
 *
 *	@subsubsection idhan-tags-info-siblings Siblings
 * 		The IDHAN Siblings system is not like the Hydrus sibling system. Instead siblings are tags that 'fight' over which is displayed (Like real siblings....It sucked being the youngest....)
 *
 * 		In cases where a rating system is used having the tag 'rating:safe' doesn't make sense when the tag 'rating:explicit' is present. This means you can have 'rating:explicit' sibling the tag 'rating:safe' where the first sibling will win against the second.
 *
 * 		Sibling are recursive. This means that `rating:explicit` will win against `rating:questionable` and `rating:questionable` will win against `rating:safe`. This also means that `rating:explicit` will win against `rating:safe` even if `rating:questionable` was not displayed in this first place.
 *
 * 		\note This is a one way relation. Infinite loops are forbidden and will error when one is made.
 *
 * 		\note In the event of 'branches' the tag will only go as high as itself. Example:
 * 		\dot Example (In this case assume tags C, F, and Z are being displayed. Outcome is only displaying tags F and C) (Blue means active and displaying, Red means active but 'lost' so it doesn't display)
 * 		digraph example_1 {
 *
 * 		F[color=blue];
 * 		C[color=blue];
 * 		Z[color=red];
 *
 * 		A -> B;
 * 		B -> D;
 * 		A -> C;
 * 		C -> D;
 * 		B -> F;
 * 		F -> Z[color=blue];
 * 		E -> Z[color=blue];
 * 		C -> E[color=blue];
 * 		}
 * 		\enddot
 *
 *
 *	@subsection idhan-tags-info-combined All the systems combined.
 * 		The following is a graph showing an example setup that could be used.
 *
 *		- Blue lines indicate a parent-child relationship (Arrow points to child)
 *		- Red lines indicate a sibling relationship (Arrow points to 'loser')
 *		- Purple lines indicate an overwrite (Arrow points to 'better' tag)
 *
 *	 	\dot
 *	 	digraph example_2 {
 *	 	node[shape=rect];
 *	 	r_explicit[label="rating:explicit"];
 *	 	r_safe[label="rating:safe"];
 *	 	r_q_misstag[label="rating:q"];
 *	 	r_questionable[label="rating:questionable"];
 *
 *	 	#overwrites
 *	 	r_q_misstag -> r_questionable[color=purple];
 *
 *
 *	 	#siblings
 *	 	r_explicit -> r_questionable[color=red];
 *	 	r_questionable -> r_safe[color=red];
 *
 *	 	#parent/child
 *	 	nipples -> r_questionable[color=blue];
 *	 	sex -> r_explicit[color=blue];
 *	 	}
 *	 	\enddot
 *
 *	 	If any rating tag above rating:safe exists then it'll be displayed 
*/

#include <vector>

#include "IDHANExceptions.hpp"

class IDHANTagException : public IDHANDatabaseException {};

//! Thrown in the event of a infinite loop of tags (parent/child, siblings, or overwrite)
/**
 * TagExceptionRecurse is thrown in the event of a infinite loop of tags.
 *
 * If you are adding tag D to tag A<br>
 * But a sibling chain already exists like: A -> B -> C -> D<br>
 * In this case A -> B -> C -> D -> A would be an infinite loop. A throw happens with the following parameters
 * - Path will consist of {4,3,2,1}
 * - target_id will be {1}
 * - source_id will be {4}
 *
 *
 */
class TagExceptionRecurse : public IDHANTagException
{
  public:
	//! Tag that is winning
	uint64_t source_id;

	//! Tag that is loosing
	uint64_t target_id;

	//! REVERSED Path from the added tag back to the origin that already exists.
	/*!
	 * @warning The path you are given is actually reversed. It goes from the end of the loop to tbe beginning.
	 * If you have A -> B -> C the path will actually be reversed {3, 2, 1}
	 * */
	std::vector< uint64_t > path;
};

//! Thrown in the event of a tag_id parameter being invalid (non existant)
 class NonExistentTagID : public IDHANTagException
 {
	 //! Id of the tag
	 uint64_t tag_id;
 };

 //! Thrown in the event of a tag not existing (text)
 class NonExistentTag : public IDHANTagException
 {
	 //! Text of the tag
	 std::string tag_text;
 };


//! A tag is a set of metainfo about a file.
/*!
 * Meta info can come in many forms. Tags are a good example of them. Tags can be placed onto a file in order to specify additional and searchable information about the file.
 * */
class Tag
{

	//! List of siblings to 'win' against
	std::vector<uint64_t> sibling_list;

	//!

	//! What overwrites this tag (0 if nothing)
	uint64_t overwitten_by {0};


  public:
	//! ID for this specific tag
	uint64_t tag_id;

  	//! Text of the 'subtag'
  	std::string subtag_text;
	//! ID of the subtag
	uint32_t subtag_id;

	//! Text of the 'group'
	std::string group_text;
	//! ID of the group
	uint16_t group_id;

	//! Returns a the full tag
	/**
	 * @returns 'group:subtag' or just 'subtag'
	 */
	std::string concat_tag() { return group_text.empty() ? subtag_text : group_text + ":" + subtag_text; }

	//! Adds a sibling (this -> that)
	/**
	 * @param tag_id tag_id of the tag to 'win' over
	 * @throws TagExceptionRecurse
	 * @throws NonExistentTag
	 */
	void addSibling(uint64_t tag_id);

	//! Removes a sibling from the sibling list the tag
	/**
	 * @note Will not fail even if the tag doesn't exist or is in the list.
	 * @param tag_id tag_id to remove as a sibling.
	 */
	void removeSibling(uint64_t tag_id);

	//! Adds a parent to the tag
	/**
	 * Adding a parent to the tag will cause images with this tag to also display it's parents
	 * @throws TagExceptionRecurse
	 * @throws NonExistentTag
	 */
	void addParent(uint64_t tag_id);

	//! Removes a parent from the tag
	/**
	 * @note Will not fail even if the tag doesn't exist or is in the list
	 * @param tag_id tag_id to remove from the parent list
	 */
	void removeParent(uint64_t tag_id);


	//! Sets a 'better' tag to render as
	/**
	 * \note Giving 0 will remove the overwrite. But you should instead use `eraseBetterTag()`
	 * @param tag_id tag to have overwrite this one
	 * @throws NonExistentTag
	 */
	void setBetterTag(uint64_t tag_id);

	//! Removes the 'better' tag to render as
	void eraseBetterTag();








};





#endif	// IDHAN_TAG_HPP
