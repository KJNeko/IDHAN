#include "js/bindings/HtmlBindings.hpp"

#include <lexbor/css/css.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/html/html.h>
#include <lexbor/selectors/selectors.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "js/CommonBindings.hpp"
#include "js/QuickJSBinding.hpp"

namespace idhan::downloader::quickjs
{

JSClassID document_class_id {};
JSClassID element_class_id {};

//! Every handle into a document keeps it alive; the nodes it hands out are owned by the document.
using DocumentPtr = std::shared_ptr< lxb_html_document_t >;

struct NodeHandle
{
	DocumentPtr document {};
	lxb_dom_node_t* node {};
};

struct CssParserDeleter
{
	void operator()( lxb_css_parser_t* parser ) const noexcept { lxb_css_parser_destroy( parser, true ); }
};

struct SelectorsDeleter
{
	void operator()( lxb_selectors_t* selectors ) const noexcept { lxb_selectors_destroy( selectors, true ); }
};

struct SelectorListDeleter
{
	void operator()( lxb_css_selector_list_t* list ) const noexcept { lxb_css_selector_list_destroy_memory( list ); }
};

class SelectorQuery
{
  public:

	explicit SelectorQuery( const std::string_view selector ) :
	  m_parser { lxb_css_parser_create() },
	  m_selectors { lxb_selectors_create() }
	{
		if ( m_parser == nullptr || lxb_css_parser_init( m_parser.get(), nullptr ) != LXB_STATUS_OK )
			throw std::runtime_error { "Unable to initialize the CSS selector parser" };

		if ( m_selectors == nullptr || lxb_selectors_init( m_selectors.get() ) != LXB_STATUS_OK )
			throw std::runtime_error { "Unable to initialize the CSS selector engine" };

		m_list.reset( lxb_css_selectors_parse(
			m_parser.get(), reinterpret_cast< const lxb_char_t* >( selector.data() ), selector.size() ) );

		if ( m_list == nullptr || m_parser->status != LXB_STATUS_OK )
			throw std::invalid_argument { "Invalid CSS selector" };

		lxb_selectors_opt_set( m_selectors.get(), LXB_SELECTORS_OPT_MATCH_FIRST );
	}

	SelectorQuery( const SelectorQuery& ) = delete;
	SelectorQuery& operator=( const SelectorQuery& ) = delete;

	std::vector< lxb_dom_node_t* > find( lxb_dom_node_t* root )
	{
		std::vector< lxb_dom_node_t* > nodes {};
		const lxb_status_t status { lxb_selectors_find( m_selectors.get(), root, m_list.get(), collectNode, &nodes ) };

		if ( status != LXB_STATUS_OK ) throw std::runtime_error { "CSS selector search failed" };

		return nodes;
	}

  private:

	static lxb_status_t collectNode( lxb_dom_node_t* node, lxb_css_selector_specificity_t, void* context )
	{
		auto* nodes { static_cast< std::vector< lxb_dom_node_t* >* >( context ) };
		nodes->emplace_back( node );
		return LXB_STATUS_OK;
	}

	std::unique_ptr< lxb_css_parser_t, CssParserDeleter > m_parser {};
	std::unique_ptr< lxb_selectors_t, SelectorsDeleter > m_selectors {};
	std::unique_ptr< lxb_css_selector_list_t, SelectorListDeleter > m_list {};
};

NodeHandle* documentHandle( JSContext* context, const JSValueConst value )
{
	return static_cast< NodeHandle* >( JS_GetOpaque2( context, value, document_class_id ) );
}

NodeHandle* elementHandle( JSContext* context, const JSValueConst value )
{
	return static_cast< NodeHandle* >( JS_GetOpaque2( context, value, element_class_id ) );
}

JSValue newElement( JSContext* context, const DocumentPtr& document, lxb_dom_node_t* node )
{
	JSValue value { JS_NewObjectClass( context, static_cast< int >( element_class_id ) ) };

	if ( JS_IsException( value ) ) return value;

	auto handle { std::make_unique< NodeHandle >( document, node ) };
	JS_SetOpaque( value, handle.release() );
	return value;
}

JSValue querySelectorAll( JSContext* context, NodeHandle* handle, const int argument_count, JSValueConst* arguments )
{
	if ( handle == nullptr ) return JS_EXCEPTION;

	if ( argument_count == 0 ) return JS_ThrowTypeError( context, "querySelectorAll requires a selector" );

	try
	{
		const auto selector { toString( context, arguments[ 0 ] ) };

		if ( !selector.has_value() ) return JS_EXCEPTION;

		SelectorQuery query { *selector };
		const auto nodes { query.find( handle->node ) };
		JSValue result { JS_NewArray( context ) };

		for ( std::size_t index {}; index < nodes.size(); ++index )
		{
			JSValue element { newElement( context, handle->document, nodes[ index ] ) };

			if ( JS_IsException( element ) )
			{
				JS_FreeValue( context, result );
				return element;
			}

			JS_SetPropertyUint32( context, result, static_cast< std::uint32_t >( index ), element );
		}

		return result;
	}
	catch ( const std::invalid_argument& exception )
	{
		return JS_ThrowSyntaxError( context, "%s", exception.what() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

JSValue querySelector( JSContext* context, NodeHandle* handle, const int argument_count, JSValueConst* arguments )
{
	if ( handle == nullptr ) return JS_EXCEPTION;

	if ( argument_count == 0 ) return JS_ThrowTypeError( context, "querySelector requires a selector" );

	try
	{
		const auto selector { toString( context, arguments[ 0 ] ) };

		if ( !selector.has_value() ) return JS_EXCEPTION;

		SelectorQuery query { *selector };
		const auto nodes { query.find( handle->node ) };

		if ( nodes.empty() ) return JS_NULL;

		return newElement( context, handle->document, nodes.front() );
	}
	catch ( const std::invalid_argument& exception )
	{
		return JS_ThrowSyntaxError( context, "%s", exception.what() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

JSValue attribute( JSContext* context, NodeHandle* handle, const std::string_view name, const bool null_when_missing )
{
	if ( handle == nullptr ) return JS_EXCEPTION;

	auto* element { lxb_dom_interface_element( handle->node ) };
	std::size_t value_size {};
	const lxb_char_t* value { lxb_dom_element_get_attribute(
		element, reinterpret_cast< const lxb_char_t* >( name.data() ), name.size(), &value_size ) };

	if ( value == nullptr ) return null_when_missing ? JS_NULL : JS_NewString( context, "" );

	return JS_NewStringLen( context, reinterpret_cast< const char* >( value ), value_size );
}

IDHAN_QJS_FUNCTION( documentQuerySelector )
{
	return querySelector( context, documentHandle( context, this_value ), argument_count, arguments );
}

IDHAN_QJS_FUNCTION( documentQuerySelectorAll )
{
	return querySelectorAll( context, documentHandle( context, this_value ), argument_count, arguments );
}

IDHAN_QJS_FUNCTION( elementQuerySelector )
{
	return querySelector( context, elementHandle( context, this_value ), argument_count, arguments );
}

IDHAN_QJS_FUNCTION( elementQuerySelectorAll )
{
	return querySelectorAll( context, elementHandle( context, this_value ), argument_count, arguments );
}

IDHAN_QJS_FUNCTION( elementGetAttribute )
{
	if ( argument_count == 0 ) return JS_ThrowTypeError( context, "getAttribute requires an attribute name" );

	try
	{
		const auto name { toString( context, arguments[ 0 ] ) };

		if ( !name.has_value() ) return JS_EXCEPTION;

		return attribute( context, elementHandle( context, this_value ), *name, true );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( elementHasAttribute )
{
	if ( argument_count == 0 ) return JS_ThrowTypeError( context, "hasAttribute requires an attribute name" );

	try
	{
		NodeHandle* handle { elementHandle( context, this_value ) };

		if ( handle == nullptr ) return JS_EXCEPTION;

		const auto name { toString( context, arguments[ 0 ] ) };

		if ( !name.has_value() ) return JS_EXCEPTION;

		const bool present { lxb_dom_element_has_attribute(
			lxb_dom_interface_element( handle->node ),
			reinterpret_cast< const lxb_char_t* >( name->data() ),
			name->size() ) };
		return JS_NewBool( context, present );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

#define IDHAN_ELEMENT_ATTRIBUTE_GETTER( function_name, attribute_name )                                                \
	IDHAN_QJS_GETTER( function_name )                                                                                  \
	{                                                                                                                  \
		return attribute( context, elementHandle( context, this_value ), attribute_name, false );                      \
	}

IDHAN_ELEMENT_ATTRIBUTE_GETTER( elementHref, "href" )
IDHAN_ELEMENT_ATTRIBUTE_GETTER( elementSrc, "src" )
IDHAN_ELEMENT_ATTRIBUTE_GETTER( elementId, "id" )
IDHAN_ELEMENT_ATTRIBUTE_GETTER( elementClassName, "class" )
IDHAN_ELEMENT_ATTRIBUTE_GETTER( elementValue, "value" )

#undef IDHAN_ELEMENT_ATTRIBUTE_GETTER

IDHAN_QJS_GETTER( elementTagName )
{
	NodeHandle* handle { elementHandle( context, this_value ) };

	if ( handle == nullptr ) return JS_EXCEPTION;

	std::size_t size {};
	const lxb_char_t* name { lxb_dom_element_qualified_name_upper( lxb_dom_interface_element( handle->node ), &size ) };

	if ( name == nullptr ) return JS_NewString( context, "" );

	return JS_NewStringLen( context, reinterpret_cast< const char* >( name ), size );
}

IDHAN_QJS_GETTER( elementTextContent )
{
	NodeHandle* handle { elementHandle( context, this_value ) };

	if ( handle == nullptr ) return JS_EXCEPTION;

	std::size_t size {};
	const lxb_char_t* text { lxb_dom_node_text_content( handle->node, &size ) };

	if ( text == nullptr ) return JS_NewString( context, "" );

	return JS_NewStringLen( context, reinterpret_cast< const char* >( text ), size );
}

void documentFinalizer( JSRuntime*, const JSValue value )
{
	delete static_cast< NodeHandle* >( JS_GetOpaque( value, document_class_id ) );
}

void elementFinalizer( JSRuntime*, const JSValue value )
{
	delete static_cast< NodeHandle* >( JS_GetOpaque( value, element_class_id ) );
}

bool installClassPrototype(
	JSContext* context,
	JSClassID& class_id,
	const JSClassDef& definition,
	const JSCFunctionListEntry* entries,
	const int entry_count )
{
	if ( class_id == 0 ) JS_NewClassID( &class_id );

	JSRuntime* runtime { JS_GetRuntime( context ) };

	if ( !JS_IsRegisteredClass( runtime, class_id ) && JS_NewClass( runtime, class_id, &definition ) < 0 ) return false;

	JSValue prototype { JS_NewObject( context ) };

	if ( JS_IsException( prototype ) ) return false;

	if ( JS_SetPropertyFunctionList( context, prototype, entries, entry_count ) < 0 )
	{
		JS_FreeValue( context, prototype );
		return false;
	}

	JS_SetClassProto( context, class_id, prototype );
	return true;
}

bool installHTMLBindings( JSContext* context )
{
	static const JSClassDef document_definition {
		.class_name = "Document",
		.finalizer = documentFinalizer,
		.gc_mark = nullptr,
		.call = nullptr,
		.exotic = nullptr,
	};
	static const JSCFunctionListEntry document_entries[] {
		IDHAN_QJS_METHOD( "querySelector", 1, documentQuerySelector ),
		IDHAN_QJS_METHOD( "querySelectorAll", 1, documentQuerySelectorAll ),
	};
	static const JSClassDef element_definition {
		.class_name = "Element",
		.finalizer = elementFinalizer,
		.gc_mark = nullptr,
		.call = nullptr,
		.exotic = nullptr,
	};
	static const JSCFunctionListEntry element_entries[] {
		IDHAN_QJS_METHOD( "querySelector", 1, elementQuerySelector ),
		IDHAN_QJS_METHOD( "querySelectorAll", 1, elementQuerySelectorAll ),
		IDHAN_QJS_METHOD( "getAttribute", 1, elementGetAttribute ),
		IDHAN_QJS_METHOD( "hasAttribute", 1, elementHasAttribute ),
		IDHAN_QJS_READONLY_PROPERTY( "href", elementHref ),
		IDHAN_QJS_READONLY_PROPERTY( "src", elementSrc ),
		IDHAN_QJS_READONLY_PROPERTY( "id", elementId ),
		IDHAN_QJS_READONLY_PROPERTY( "className", elementClassName ),
		IDHAN_QJS_READONLY_PROPERTY( "value", elementValue ),
		IDHAN_QJS_READONLY_PROPERTY( "tagName", elementTagName ),
		IDHAN_QJS_READONLY_PROPERTY( "textContent", elementTextContent ),
	};

	const bool document_installed { installClassPrototype(
		context,
		document_class_id,
		document_definition,
		document_entries,
		static_cast< int >( std::size( document_entries ) ) ) };
	const bool element_installed { installClassPrototype(
		context,
		element_class_id,
		element_definition,
		element_entries,
		static_cast< int >( std::size( element_entries ) ) ) };
	return document_installed && element_installed;
}

IDHAN_QJS_FUNCTION( parseHTML )
{
	(void)this_value;

	if ( argument_count == 0 ) return JS_ThrowTypeError( context, "idhan.parseHtml requires HTML text" );

	try
	{
		const auto html { toString( context, arguments[ 0 ] ) };

		if ( !html.has_value() ) return JS_EXCEPTION;

		lxb_html_document_t* document { lxb_html_document_create() };

		if ( document == nullptr ) return JS_ThrowOutOfMemory( context );

		DocumentPtr data { document, lxb_html_document_destroy };

		if ( lxb_html_document_parse( document, reinterpret_cast< const lxb_char_t* >( html->data() ), html->size() )
		     != LXB_STATUS_OK )
		{
			return JS_ThrowInternalError( context, "Unable to parse HTML" );
		}

		JSValue value { JS_NewObjectClass( context, static_cast< int >( document_class_id ) ) };

		if ( JS_IsException( value ) ) return value;

		auto handle { std::make_unique< NodeHandle >( std::move( data ), lxb_dom_interface_node( document ) ) };
		JS_SetOpaque( value, handle.release() );
		return value;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}
} // namespace idhan::downloader::quickjs
