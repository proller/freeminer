#pragma once

#include <memory>
#include <string>

class MapgenEarth;
class handler_i;

namespace earth_osmium_detail
{

std::shared_ptr<handler_i> make_handler(MapgenEarth *mg, const std::string &path);

} // namespace earth_osmium_detail
