// Copyright 2020-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// BOOST_CLASS_EXPORT_KEY (used throughout core/hle/kernel and elsewhere) is header-only and needed
// regardless of whether the compiled archive backend below is available, so it is always included.
#include <boost/serialization/export.hpp>

#if defined(AZAHAR_VITA)

// Savestates are not part of the Vita port's scope (see docs/vita-port.md): binary_iarchive and
// binary_oarchive pull in the compiled boost::archive/boost::iostreams libraries, which are not
// linked on Vita. iarchive and oarchive are declared but never defined, so a stray real usage
// fails to compile instead of silently linking against nothing; SERIALIZE_IMPL and
// SERIALIZE_EXPORT_IMPL become no-ops so the `ar & ...` member bodies stay valid, uninstantiated
// template code.
class iarchive;
class oarchive;

#define SERIALIZE_IMPL(A)
#define SERIALIZE_EXPORT_IMPL(A)

#define DEBUG_SERIALIZATION_POINT                                                                  \
    do {                                                                                           \
    } while (0)

#else

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

using iarchive = boost::archive::binary_iarchive;
using oarchive = boost::archive::binary_oarchive;

#define SERIALIZE_IMPL(A)                                                                          \
    template void A::serialize<iarchive>(iarchive & ar, const unsigned int file_version);          \
    template void A::serialize<oarchive>(oarchive & ar, const unsigned int file_version);

#define SERIALIZE_EXPORT_IMPL(A)                                                                   \
    BOOST_CLASS_EXPORT_IMPLEMENT(A)                                                                \
    BOOST_SERIALIZATION_REGISTER_ARCHIVE(iarchive)                                                 \
    BOOST_SERIALIZATION_REGISTER_ARCHIVE(oarchive)

#define DEBUG_SERIALIZATION_POINT                                                                  \
    do {                                                                                           \
        LOG_DEBUG(Savestate, "");                                                                  \
    } while (0)

#endif
