#pragma once
#ifndef MRMIME_HPP_INCLUDED
#define MRMIME_HPP_INCLUDED

/*MMMM               MMMMMM
M:::::M             M:::::M
M::::::M           M::::::M
M:::::::M         M:::::::M
M::::::::M       M::::::::M  rrrr   rrrrrrrrr
M:::::::::M     M:::::::::M  r:::rrr:::::::::r
M:::::M::::M   M::::M:::::M  r::::::::::::::::r
M::::M M::::M M::::M M::::M  r::::::rrrrr::::::r
M::::M  M::::M::::M  M::::M  r:::::r     r:::::r
M::::M   M:::::::M   M::::M  r:::::r     rrrrrrr
M::::M    M:::::M    M::::M  r:::::r
M::::M     MMMMM     M::::M  r:::::r
M::::M               M::::M  r:::::r
M::::M               M::::M  r:::::r             :::::.
M::::M               M::::M  r:::::r             ::::::
MMMMMM               MMMMMM  rrrrrrr             ::::::


MMMMMM               MMMMMM   iiii
M:::::M             M:::::M  i::::i
M::::::M           M::::::M   iiii
M:::::::M         M:::::::M
M::::::::M       M::::::::M  iiiiii     mmmmmm   mmmmmm        eeeeeeeeeeee
M:::::::::M     M:::::::::M  I::::i   mm::::::m m::::::mm    ee::::::::::::ee
M:::::M::::M   M::::M:::::M  i::::i  m:::::::::m:::::::::m  e::::::eeeee:::::ee
M::::M M::::M M::::M M::::M  i::::i  m:::::::::::::::::::m  e:::::e     e:::::e
M::::M  M::::M::::M  M::::M  i::::i  m::::mmm:::::mmm::::m  e::::::eeeee::::::e
M::::M   M:::::::M   M::::M  i::::i  m:::m   m:::m   m:::m  e::::::::::::::::e
M::::M    M:::::M    M::::M  i::::i  m:::m   m:::m   m:::m  e:::::eeeeeeeeeee
M::::M     MMMMM     M::::M  i::::i  m:::m   m:::m   m:::m  e::::::e
M::::M               M::::M  i::::i  m:::m   m:::m   m:::m  e:::::::e
M::::M               M::::M  i::::i  m:::m   m:::m   m:::m  e::::::::eeeeeeee
M::::M               M::::M  i::::i  m:::m   m:::m   m:::m   ee:::::::::::::e
MMMMMM               MMMMMM  iiiiii  mmmmm   mmmmm   mmmmm     eeeeeeeeeeeee


               ,---.    ___
               |(__)| .',-.`.
               `.  j  | \.'.'
               _'  `"'  ,-'___
             ,"         `"',--.\                     _..--.
             |           __`..''                _,.-'      `-.
   ,-""'`-.. '          (  `""'         _...--"'        ,.--..'
 ,'        .' `._____  ,.`-..--"""'----.               /
/   _..._,'   ."     \ `..'|            `.        ___.'
'.-"  .'    ,'        '---"               `.     /
     |    .'.._     ,'                      `.  /
      \   `    `._ /      |  !    !  |        |"
       `.  `.     |   __  |          j  ,--. _|..._
         \   `.""""-.'  `. '           /  ,'"      `-.
          `.   `.    `.  |   _____|   |  /            `.
          |`....'     |_,'   `.   '    `.              |
          |           |---....____....-"`        .--.  '
           .         ,'                  `..._  (    `'
            `--..,.-'      _.--"""'"'.   |,"".`. ,--.. \
                 |       ,'       ."""`. ``-" | |(__)|  `.
                 |      .         |(__) `-'   '"   ,"     |
                / `     |          `--.          .'_,..-"'
               /   `._   ._       ."""`-         ||
               '..._  `._  `-....( (__) __    _.','
              ,'    `.   `---.....`..-"'  `"'"_,".
         ,-""`. _.---+..-'            `"---+-'   `
        /      `.                       ." , \    \
        ._    ,--.                     |  |  |.    \
         _:--'    `.                   |  `._| `..-|
     ,-'"  `.    .' |                  '    `"--...'-.
    .        `""'_.-'                   `.           |
    |         ,-'                         `-.______,.'
    '.     _.'
      `---'

Written by Alaestor Weissman 2021
https://github.com/alaestor
http://discord.0x04.cc ( Honshitsu#9218 ) */

#include <tuple>
#include <array>

#include "filetype_signature_definitions.hpp"
#include "filetype_strings.hpp"

namespace MrMime {

typedef std::array<std::byte, internal::size_of_largest_signature>
	header_data_buffer_t;

/// Attempts to match a signature with an array of bytes
// returns FileType::APPLICATION_UNKNOWN if no signature can be matched.
[[nodiscard]] FileType deduceFileType(const header_data_buffer_t& header_data)
{
	using namespace MrMime::internal;
	return std::apply(
		[&header_data]<typename ... SIGS>(const SIGS& ... sigs)
		-> FileType
		{
			FileType result{ APPLICATION_UNKNOWN };

			/// Would be undefined behavior if header_data was < sig.size()
			// Note: header_data_buffer_t is guaranteed to be >= sig.size()
			(sigs.compare(result, header_data) || ...);

			return result;
		},
		signatures
	);
}

} // namespace MrMime

#endif // MRMIME_HPP_INCLUDED
