// Boost.Bimap
//
// Copyright (c) 2006-2007 Matias Capeletto
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.lslboost.org/LICENSE_1_0.txt)

/// \file property_map/unordered_set_support.hpp
/// \brief Support for the property map concept.

#ifndef BOOST_BIMAP_PROPERTY_MAP_UNORDERED_SET_SUPPORT_HPP
#define BOOST_BIMAP_PROPERTY_MAP_UNORDERED_SET_SUPPORT_HPP

#if defined(_MSC_VER) && (_MSC_VER>=1200)
#pragma once
#endif

#include <lslboost/config.hpp>

#include <lslboost/property_map/property_map.hpp>
#include <lslboost/bimap/unordered_set_of.hpp>
#include <lslboost/bimap/support/data_type_by.hpp>
#include <lslboost/bimap/support/key_type_by.hpp>

#ifndef BOOST_BIMAP_DOXYGEN_WILL_NOT_PROCESS_THE_FOLLOWING_LINES

namespace lslboost {

template< class Tag, class Bimap >
struct property_traits< ::lslboost::bimaps::views::unordered_map_view<Tag,Bimap> >
{
    typedef BOOST_DEDUCED_TYPENAME
        ::lslboost::bimaps::support::data_type_by<Tag,Bimap>::type value_type;
    typedef BOOST_DEDUCED_TYPENAME
        ::lslboost::bimaps::support:: key_type_by<Tag,Bimap>::type   key_type;

    typedef readable_property_map_tag category;
};


template< class Tag, class Bimap >
const BOOST_DEDUCED_TYPENAME ::lslboost::bimaps::support::data_type_by<Tag,Bimap>::type &
    get(const ::lslboost::bimaps::views::unordered_map_view<Tag,Bimap> & m,
        const BOOST_DEDUCED_TYPENAME
            ::lslboost::bimaps::support::key_type_by<Tag,Bimap>::type & key)
{
    return m.at(key);
}

} // namespace lslboost

#endif // BOOST_BIMAP_DOXYGEN_WILL_NOT_PROCESS_THE_FOLLOWING_LINES

#endif // BOOST_BIMAP_PROPERTY_MAP_UNORDERED_SET_SUPPORT_HPP
