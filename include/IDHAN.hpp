//
// Created by kj16609 on 8/16/22.
//

#ifndef IDHAN_IDHAN_HPP
#define IDHAN_IDHAN_HPP

/**
 * @page page-idhan-features Feature Set
 * @section page-idhan-features-basics Basics
 * @subsection page-idhan-features-basics-database Database Support
 * 		IDHAN supports two major database backends. sqlite3 `IDHANSqlite` and postgresql `IDHANPostgresql`. Either of these are selected at compile time.
 *
 *		\note Select what you need. Not what you want. If you do not need the performance for a massive dataset then use sqlite3
 *
 *		Pros and cons
 *		Pro/Con | Postgresql | Sqlite
 *		---|-----------|-------
 *		Support | <span style="color:green">First to get support</span> | <span style="color:red">Support might come late.</span>
 *		Performance | <span style="color:green">Performance is examined thoroughly</span> | <span style="color:red">Performance is not a priority</span>
 *		Size | <span style="color:green">Reduced size due to data types</span> | <span style="color:red">Generic data types meaning we have less control over what types are used and cannot use the smallest types to fit the data</span>
 *		Performance with size |<span style="color:green">Table partitioning for rapid access even with massive tables</span> | <span style="color:red">No table partitioning implemented (If it even has it?)</span>
 *		Threading | <span style="color:green">Multi thread access</span> | <span style="color:red">Blocking</span>
 *		Complexity | <span style="color:red">Complicated to setup</span> | <span style="color:green">Easy to setup</span>
 *		Active Backups | <span style="color:green">Capable of writing to two databases at once (One local, One offsite)</span> | <span style="color:red">All or nothing. Backup stuff exists for sqlite3 but it's not very good</span>
 *		User Backups | <span style="color:red">Better know some SQL dear user.</span> | <span style="color:green">zip go brrrrr</span>
 *		Portability | <span style="color:red">Only if you access it across a network is it considered 'portable'</span> | <span style="color:green">Absolutely.</span>
 *		System | <span style="color:yellow">Works on most systems</span> | <span style="color:green">Works on a potato</span>
 *
 *		\remarks If you are building an application like Hydrus with a lot of interconnected systems. Use postgresql.
 *
 *		\remarks If you are building an **SMALL** applocation for managing your family photos? Use sqlite3
 *
 *
 * @section page-idhan-features-advanced Advanced
 * @subsection page-idhan-features-flyweight Flyweight design
 * 		IDHAN supports a flyweight design that reduces memory usage for redundent data by keeping a large pool of `FileDataContainer` that is handed out through pointers contained in `FileData`. This has the bonus of allowing the object to stay in sync with the database at all times.
 *
 * 		\note In the event that you don't wish for this system to be used then you can check the define list to disabled it in your program.
 *
 * 		\warning Disabling this will remove some functionality within `FileData` that allows the object to remain in sync with the database so thus it needs to re-check every time you access the object. Crippling your performance. Disabling this is ***NOT RECOMENDED***
 *
 * 		\warning Disabling this feature will **DRASTICALLY** increase your memory usage
 *
 * @subsection page-idhan-features-markings Markings
 * 		Markings are an attachment to files where the database will store additional json that connects to a file. This can be used for adding functionality/data to the database that otherwise isn't present or implemented.
 *
 * 		Examples of this would be adding a way to keep track of if a file has been processed or not by a 3rd party program.
 *
 * 		\note While still advanced these are made to be a simpler alternative to custom tables. While being slower and a bit harder to access
 *
 * 		\remark This system is supposed to be used for additional data. If you want something searchable then consider looking into @ref page-idhan-features-direct-db
 *
 * 	@subsection page-idhan-features-tables Direct Database access
 * 		While IDHAN is an abstraction to the database side of things we still give you the full ability to access the database and make your own queries. It is recomended to follow our @ref page-idhan-database-direct-guide on how to make the most of each db backend
 *
 * 		\note While we provide this system to you we do not offer any types of performnace enhancements along with it.
 *
 * 		\warning There is **NO** safety attached to this system. If you `drop tables files` it will do so and it will not be possible to recover without a backup. Be careful.
 *
 * @page page-idhan-database-direct-guide Direct Database Access Guide
 * @section Postgresql
 * @subsection Creating efficent tables
 * @subsection Efficent queries
 * @subsection Helper functions
 *
 * @section sqlite3
 * @subsection Creating efficent tables
 * @subsection Queries
 * @subsection Helper functions
 *
 *
 */




















#endif	// IDHAN_IDHAN_HPP
