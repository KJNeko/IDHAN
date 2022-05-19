//
// Created by kj16609 on 5/18/22.
//

#include "files.hpp"

#include "include/utility/cache.hpp"

#include "connection.hpp"

namespace idhan::files
{
	/*void importFile(std::filesystem::path file)
	{
		std::vector<char> buffer {fgl::read_binary_file<char>(file)};

		Connection conn;
		pqxx::work w(conn.getConn());


		const std::string str = std::string(buffer.begin(), buffer.end());



		return;
	}*/

	uint64_t getHashID(std::vector<char> hash)
	{
		std::stringstream ss;
		for(auto c : hash)
		{
			ss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(static_cast<unsigned char>(c));
		}

		const std::string str = ss.str();

		static Cache<std::string, uint64_t> cache;

		auto ret = cache.get(str);

		if(ret.has_value())
		{

			return ret.value();
		}

		Connection conn;
		pqxx::work w(conn.getConn());


		pqxx::result r = w.exec_prepared("SELECTFILE", str);

		if(r.size() == 0)
		{
			r = w.exec_prepared("INSERTFILE", str);
		}
		w.commit();

		cache.place(str, r[0][0].as<uint64_t>());

		return r[0][0].as<uint64_t>();
	}


}