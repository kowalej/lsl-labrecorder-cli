//  Copyright 2009-2010 Vicente J. Botet Escriba

//  Distributed under the Boost Software License, Version 1.0.
//  See http://www.boost.org/LICENSE_1_0.txt

#ifndef BOOST_CHRONO_DETAIL_SYSTEM_HPP
#define BOOST_CHRONO_DETAIL_SYSTEM_HPP

#if !defined BOOST_CHRONO_DONT_PROVIDE_HYBRID_ERROR_HANDLING

#include <boost/system/error_code.hpp>

#define BOOST_CHRONO_SYSTEM_CATEGORY lslboost::system::system_category()

#define BOOST_CHRONO_THROWS lslboost::throws()
#define BOOST_CHRONO_IS_THROWS(EC) (&EC==&lslboost::throws())

#endif
#endif
