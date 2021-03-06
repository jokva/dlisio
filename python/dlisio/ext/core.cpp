#include <bitset>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>
#include <pybind11/stl.h>
#include <datetime.h>

#include <dlisio/dlisio.h>
#include <dlisio/types.h>

namespace py = pybind11;
using namespace py::literals;

#include <dlisio/ext/exception.hpp>
#include <dlisio/ext/io.hpp>
#include <dlisio/ext/types.hpp>

namespace pybind11 { namespace detail {

/*
 * Register boost::optional and mpark::variant type casters, since C++17 is not
 * a requirement yet, and auto-conversion from optional to None/object and
 * auto-variant-extraction is desired.
 *
 * https://pybind11.readthedocs.io/en/stable/advanced/cast/stl.html
 */

template < typename... T >
struct type_caster< mpark::variant< T... > > :
    variant_caster< mpark::variant< T... > > {};

/*
 * Automate the conversion of strong typedefs to python type that corresponds
 * to the underlying data type (as returned by dl::decay).
 */
template < typename T >
struct dlis_caster {
    PYBIND11_TYPE_CASTER(T, _("dlisio.core.type.")+_(dl::typeinfo< T >::name));

    static handle cast( const T& src, return_value_policy, handle ) {
        return py::cast( dl::decay( src ) ).inc_ref();
    }

    /*
     * For now, do not succeed ever when trying to convert from a python value
     * to the corresponding C++ value, because it's not used and probably
     * requires some template specialisation
     */
    bool load( handle, bool ) { return false; }
};


template <>
struct type_caster< mpark::monostate > {
    PYBIND11_TYPE_CASTER(mpark::monostate, _("monostate"));

    static handle cast( const mpark::monostate&, return_value_policy, handle ) {
        return py::none();
    }

    bool load( handle, bool ) { return false; }
};

template <>
handle dlis_caster< dl::dtime >::cast( const dl::dtime& src, return_value_policy, handle )
{
    // TODO: add TZ info
    return PyDateTime_FromDateAndTime( src.Y,
                                       src.M,
                                       src.D,
                                       src.H,
                                       src.MN,
                                       src.S,
                                       src.MS );
}

namespace {

handle maybe_decode(const std::string& src) noexcept (false) {
    try {
        /* was valid UTF-8, all is well*/
        return py::str(src).inc_ref();
    } catch(std::runtime_error& e) {
        PyErr_Clear();
        /*
         * The degree symbol is weird in UTF-8, but often shows up
         *
         * https://stackoverflow.com/questions/8732025/why-degree-symbol-differs-from-utf-8-from-unicode
         *
         * Look for this symbol in the string - if it's there, replace it with
         * the UTF-8 one and try to return that string. If _that_ fails, return
         * bytes
         */
        auto pos = src.find('\xB0');

        // Ok, so it wasn't the degree symbol being encoded wrong - return the
        // string as bytes and defer decoding to caller
        if (pos == std::string::npos)
            return py::bytes(src).inc_ref();

        std::string source(src);
        source.insert(pos, 1, '\xC2');
        while ((pos = source.find('\xB0', pos + 2)) != std::string::npos) {
            source.insert(pos, 1, '\xC2');
        }

        /*
         * Now this should be proper unicode. If it isn't, return bytes again
         *
         * TODO: Return-as-bytes should probably not be a silent conversion
         */
        try {
            return py::str(source).inc_ref();
        } catch (std::runtime_error&) {
            PyErr_Clear();
            return py::bytes(src).inc_ref();
        }
    }
}

}

template <>
handle dlis_caster< dl::ascii >::cast(const dl::ascii& src, return_value_policy, handle)
{
    return maybe_decode(dl::decay(src));
}

template <>
handle dlis_caster< dl::ident >::cast(const dl::ident& src, return_value_policy, handle)
{
    return maybe_decode(dl::decay(src));
}

template <>
handle dlis_caster< dl::units >::cast(const dl::units& src, return_value_policy, handle)
{
    return maybe_decode(dl::decay(src));
}

/*
 * Now *register* the strong-typedef type casters with pybind, so that py::cast
 * and the pybind implicit conversion works.
 *
 * Notice that types that just alias native types (std::int32_t/slong etc.)
 * SHOULD NOT be registered this way, as the conversion already exists, and
 * would cause an infinite loop in the conversion logic.
 */
template <> struct type_caster< dl::fshort > : dlis_caster< dl::fshort > {};
template <> struct type_caster< dl::isingl > : dlis_caster< dl::isingl > {};
template <> struct type_caster< dl::vsingl > : dlis_caster< dl::vsingl > {};
template <> struct type_caster< dl::fsing1 > : dlis_caster< dl::fsing1 > {};
template <> struct type_caster< dl::fsing2 > : dlis_caster< dl::fsing2 > {};
template <> struct type_caster< dl::fdoub1 > : dlis_caster< dl::fdoub1 > {};
template <> struct type_caster< dl::fdoub2 > : dlis_caster< dl::fdoub2 > {};
template <> struct type_caster< dl::uvari  > : dlis_caster< dl::uvari  > {};
template <> struct type_caster< dl::ident  > : dlis_caster< dl::ident  > {};
template <> struct type_caster< dl::ascii  > : dlis_caster< dl::ascii  > {};
template <> struct type_caster< dl::dtime  > : dlis_caster< dl::dtime  > {};
template <> struct type_caster< dl::origin > : dlis_caster< dl::origin > {};
template <> struct type_caster< dl::status > : dlis_caster< dl::status > {};
template <> struct type_caster< dl::units  > : dlis_caster< dl::units  > {};

}} // namespace pybind11::detail

namespace {

py::dict storage_label( py::buffer b ) {
    auto info = b.request();
    if (info.size < DLIS_SUL_SIZE) {
        std::string msg =
            "buffer to small: buffer.size (which is "
            + std::to_string( info.size ) + ") < "
            + "n (which is " + std::to_string( DLIS_SUL_SIZE ) + ")"
        ;
        throw std::invalid_argument( msg );
    }

    int seqnum;
    int major;
    int minor;
    int layout;
    std::int64_t maxlen;
    char id[ 61 ] = {};
    auto err = dlis_sul( static_cast< const char* >( info.ptr ),
                         &seqnum,
                         &major,
                         &minor,
                         &layout,
                         &maxlen,
                         id );


    switch (err) {
        case DLIS_OK: break;

        // TODO: report more precisely  a lot of stuff can go wrong with the
        // SUL
        if( err == DLIS_UNEXPECTED_VALUE )
            throw py::value_error( "unable to parse storage label" );

        case DLIS_INCONSISTENT:
            runtime_warning(
                "storage unit label inconsistent with "
                "specification - falling back to assuming DLIS v1"
            );
            break;
    }

    std::string version = std::to_string( major )
        + "."
        + std::to_string( minor );

    std::string laystr = "record";
    if (layout != DLIS_STRUCTURE_RECORD) laystr = "unknown";

    return py::dict(
        "sequence"_a = seqnum,
        "version"_a = version.c_str(),
        "layout"_a = laystr.c_str(),
        "maxlen"_a = maxlen,
        "id"_a =  id
    );
}

}

PYBIND11_MODULE(core, m) {
    PyDateTime_IMPORT;

    py::register_exception_translator( []( std::exception_ptr p ) {
        try {
            if( p ) std::rethrow_exception( p );
        } catch( const dl::not_implemented& e ) {
            PyErr_SetString( PyExc_NotImplementedError, e.what() );
        } catch( const io_error& e ) {
            PyErr_SetString( PyExc_IOError, e.what() );
        } catch( const eof_error& e ) {
            PyErr_SetString( PyExc_EOFError, e.what() );
        }
    });

    m.def( "storage_label", storage_label );

    /*
     * TODO: support constructor with kwargs
     * TODO: support comparison with tuple
     * TODO: fmtlib for strings
     */
    py::class_< dl::obname >( m, "obname" )
        .def_readonly( "origin",     &dl::obname::origin )
        .def_readonly( "copynumber", &dl::obname::copy )
        .def_readonly( "id",         &dl::obname::id )
        .def( "__eq__",              &dl::obname::operator == )
        .def( "__repr__", []( const dl::obname& o ) {
            return "dlisio.core.obname(id='{}', origin={}, copynum={})"_s
                    .format( dl::decay(o.id),
                             dl::decay(o.origin),
                             dl::decay(o.copy) )
                    ;
        })
    ;

    py::class_< dl::objref >( m, "objref" )
        .def_readonly( "type", &dl::objref::type )
        .def_readonly( "name", &dl::objref::name )
        .def( "__repr__", []( const dl::objref& o ) {
            return "dlisio.core.objref(id='{}', origin={}, copynum={}, type={})"_s
                    .format( dl::decay(o.name.id),
                             dl::decay(o.name.origin),
                             dl::decay(o.name.copy),
                             dl::decay(o.type) )
                    ;
        })
    ;

    py::class_< dl::attref >( m, "attref" )
        .def_readonly( "type", &dl::attref::type )
        .def_readonly( "name", &dl::attref::name )
        .def_readonly( "label", &dl::attref::label )
        .def( "__repr__", []( const dl::attref& o ) {
            return "dlisio.core.attref(id='{}', origin={}, copynum={}, type={})"_s
                    .format( dl::decay(o.name.id),
                             dl::decay(o.name.origin),
                             dl::decay(o.name.copy),
                             dl::decay(o.type) )
                    ;
        })
    ;

    py::class_< dl::basic_object >( m, "basic_object" )
        .def_readonly( "name", &dl::basic_object::object_name )
        .def( "__len__",       &dl::basic_object::len )
        .def( "__getitem__",   &dl::basic_object::at )
        .def( "values", []( const dl::basic_object& o ) {
            auto begin = o.attributes.begin();
            auto end = o.attributes.end();
            return py::make_iterator( begin, end );
        }, py::keep_alive< 0, 1 >() )
    ;

    py::class_< dl::object_set >( m, "object_set" )
        .def_readonly( "type",    &dl::object_set::type )
        .def_readonly( "name",    &dl::object_set::name )
        .def_readonly( "objects", &dl::object_set::objects )
    ;

    py::class_< dl::object_attribute >( m, "object_attribute" )
        .def_readonly( "label", &dl::object_attribute::label )
        .def_readonly( "count", &dl::object_attribute::count )
        .def_readonly( "reprc", &dl::object_attribute::reprc )
        .def_readonly( "units", &dl::object_attribute::units )
        .def_property_readonly( "value", []( const dl::object_attribute& attr ) {
            return attr.value;
        })
        .def( "__repr__", []( const dl::object_attribute& attr ) {
            return "{}: C={} R={} U={}, V={}"_s.format(
                dl::decay( attr.label ),
                dl::decay( attr.count ),
                dl::decay( attr.reprc ),
                dl::decay( attr.units ),
                dl::decay( attr.value )
            );
        })
    ;

    py::enum_< dl::representation_code >( m, "reprc" )
        .value( "fshort", dl::representation_code::fshort )
        .value( "fsingl", dl::representation_code::fsingl )
        .value( "fsing1", dl::representation_code::fsing1 )
        .value( "fsing2", dl::representation_code::fsing2 )
        .value( "isingl", dl::representation_code::isingl )
        .value( "vsingl", dl::representation_code::vsingl )
        .value( "fdoubl", dl::representation_code::fdoubl )
        .value( "fdoub1", dl::representation_code::fdoub1 )
        .value( "fdoub2", dl::representation_code::fdoub2 )
        .value( "csingl", dl::representation_code::csingl )
        .value( "cdoubl", dl::representation_code::cdoubl )
        .value( "sshort", dl::representation_code::sshort )
        .value( "snorm" , dl::representation_code::snorm  )
        .value( "slong" , dl::representation_code::slong  )
        .value( "ushort", dl::representation_code::ushort )
        .value( "unorm" , dl::representation_code::unorm  )
        .value( "ulong" , dl::representation_code::ulong  )
        .value( "uvari" , dl::representation_code::uvari  )
        .value( "ident" , dl::representation_code::ident  )
        .value( "ascii" , dl::representation_code::ascii  )
        .value( "dtime" , dl::representation_code::dtime  )
        .value( "origin", dl::representation_code::origin )
        .value( "obname", dl::representation_code::obname )
        .value( "objref", dl::representation_code::objref )
        .value( "attref", dl::representation_code::attref )
        .value( "status", dl::representation_code::status )
        .value( "units" , dl::representation_code::units  )
    ;

    py::class_< dl::record >( m, "record", py::buffer_protocol() )
        .def_property_readonly( "explicit",  &dl::record::isexplicit )
        .def_property_readonly( "encrypted", &dl::record::isencrypted )
        .def_readonly( "consistent", &dl::record::consistent )
        .def_readonly( "type", &dl::record::type )
        .def_buffer( []( dl::record& rec ) -> py::buffer_info {
            const auto fmt = py::format_descriptor< char >::format();
            return py::buffer_info(
                rec.data.data(),    /* Pointer to buffer */
                sizeof(char),       /* Size of one scalar */
                fmt,                /* Python struct-style format descriptor */
                1,                  /* Number of dimensions */
                { rec.data.size() },/* Buffer dimensions */
                { 1 }               /* Strides (in bytes) for each index */
            );
        })
    ;

    py::class_< dl::stream >( m, "stream" )
        .def( py::init< const std::string& >() )
        .def( "reindex", &dl::stream::reindex )
        .def( "__getitem__", [](dl::stream& o, int i) { return o.at(i); })
        .def( "close", &dl::stream::close )
        .def( "get", []( dl::stream& s, py::buffer b, long long off, int n ) {
            auto info = b.request();
            if (info.size < n) {
                std::string msg =
                      "buffer to small: buffer.size (which is "
                    + std::to_string( info.size ) + ") < "
                    + "n (which is " + std::to_string( n ) + ")"
                ;
                throw std::invalid_argument( msg );
            }

            s.read( static_cast< char* >( info.ptr ), off, n );
            return b;
        })
        .def( "extract", [](dl::stream& s, const std::vector< long long >& tells) {
            std::vector< dl::record > recs;
            recs.reserve( tells.size() );
            for (auto i : tells) {
                auto rec = s.at( i );
                if (rec.isencrypted()) continue;
                recs.push_back( std::move( rec ) );
            }
            return recs;
        })
    ;

    m.def( "parse_objects", []( const std::vector< dl::record >& recs ) {
        std::vector< dl::object_set > objects;
        for (const auto& rec : recs) {
            if (rec.isencrypted()) continue;
            auto begin = rec.data.data();
            auto end = begin + rec.data.size();
            objects.push_back( dl::parse_objects( begin, end ) );
        }
        return objects;
    });

    py::class_< mio::mmap_source >( m, "mmap_source" )
        .def( py::init<>() )
        .def( "map", dl::map_source )
    ;

    m.def( "findsul", dl::findsul );
    m.def( "findvrl", dl::findvrl );

    m.def( "findoffsets", []( mio::mmap_source& file, long long from ) {
        const auto ofs = dl::findoffsets( file, from );
        return py::make_tuple( ofs.tells, ofs.residuals, ofs.explicits );
    });

    m.def( "marks", [] ( const std::string& path ) {
        mio::mmap_source file;
        dl::map_source( file, path );
        auto marks = dl::findoffsets( file, 80 );
        return py::make_tuple( marks.residuals, marks.tells );
    });
}
