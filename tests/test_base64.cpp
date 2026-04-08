#include "base64.hpp"
#include "test.hpp"
#include <libtorrent/config.hpp>
#include <libtorrent/string_view.hpp>

int main_ret = 0;

int main(int argc, char* argv[])
{
	using namespace lt::literals;

	std::pair<lt::string_view, lt::string_view> test_vectors[] = {
		{""_sv,""_sv},
		{"f"_sv,"Zg=="_sv},
		{"fo"_sv,"Zm8="_sv},
		{"foo"_sv,"Zm9v"_sv},
		{"foob"_sv,"Zm9vYg=="_sv},
		{"fooba"_sv,"Zm9vYmE="_sv},
		{"foobar"_sv,"Zm9vYmFy"_sv},
	};

	for (auto const& [input, output] : test_vectors)
	{
		TEST_CHECK(libtorrent::base64decode(std::string(output)) == input);
	}
	return main_ret;
}
