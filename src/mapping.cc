// SPDX-License-Identifier: MIT
// Copyright (c) 2004-2026 Manvel Avetisian

#include "mapping.h"

#include <stdexcept>
#include "log.h"

namespace edge::detail
{

void unmap_failed(std::exception_ptr error) noexcept
{
	try
	{
		std::rethrow_exception(error);
	}
	catch (std::exception const &e)
	{
		EDGE_LOG_ERROR("a mapping that ended in its destructor could not send what was written: {}", e.what());
	}
	catch (...)
	{
		EDGE_LOG_ERROR("a mapping that ended in its destructor could not send what was written");
	}
}

}
