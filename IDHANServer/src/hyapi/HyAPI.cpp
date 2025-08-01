//
// Created by kj16609 on 11/6/24.
//

#include "HyAPI.hpp"

#include "IDHANTypes.hpp"
#include "api/SearchAPI.hpp"
#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "api/helpers/tags/tags.hpp"
#include "constants/hydrus_version.hpp"
#include "core/search/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpClient.h"
#include "fgl/defines.hpp"
#include "fixme.hpp"
#include "helpers.hpp"
#include "logging/log.hpp"
#include "metadata/parseMetadata.hpp"
#include "versions.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::unsupported( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value root;
	root[ "status" ] = 410;
	root[ "message" ] = "IDHAN Hydrus API does not support this request";

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

// /hyapi/api_version
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::apiVersion( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value json;
	json[ "version" ] = HYDRUS_MIMICED_API_VERSION;
	json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

	// I'm unsure if anything would actually ever need this.
	// But i figured i'd supply it anyways
	json[ "is_idhan_instance" ] = true;
	json[ "idhan_api_version" ] = IDHAN_API_VERSION;

	const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

	co_return response;
}

// /hyapi/access/request_new_permissions
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::requestNewPermissions( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

// /hyapi/access/session_key
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::sessionKey( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

// /hyapi/access/verify_access_key
drogon::Task< drogon::HttpResponsePtr > HydrusAPI::verifyAccessKey( drogon::HttpRequestPtr request )
{
	Json::Value json;
	json[ "basic_permissions" ] = 0;
	json[ "human_description" ] = "";

	const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

	co_return response;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getService( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

drogon::Task< Json::Value > getServiceList( drogon::orm::DbClientPtr db )
{
	// we start with the local tags services
	const auto local_tags_result { co_await db->execSqlCoro( "SELECT tag_domain_id, domain_name FROM tag_domains" ) };

	Json::Value services {};

	for ( auto& row : local_tags_result )
	{
		Json::Value info {};
		info[ "name" ] = row[ "domain_name" ].as< std::string >();
		info[ "type" ] = hydrus::gen_constants::LOCAL_TAG;
		info[ "type_pretty" ] = "local tag service";

		const auto service_key {
			format_ns::format( "{}-{}", hydrus::gen_constants::LOCAL_TAG, row[ "tag_domain_id" ].as< TagDomainID >() )
		};

		info[ "service_key" ] = service_key;

		services[ service_key ] = std::move( info );
	}

	co_return services;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getServices( drogon::HttpRequestPtr request )
{
	const auto db { drogon::app().getDbClient() };

	Json::Value root {};

	//Deprecated in v531
	root[ "tag_repositories" ] = Json::Value( Json::arrayValue );
	root[ "local_files" ] = Json::Value( Json::arrayValue );
	root[ "local_updates" ] = Json::Value( Json::arrayValue );
	root[ "file_repositories" ] = Json::Value( Json::arrayValue );
	root[ "all_local_media" ] = Json::Value( Json::arrayValue );
	root[ "all_local_media" ] = Json::Value( Json::arrayValue );
	root[ "all_known_files" ] = Json::Value( Json::arrayValue );
	root[ "all_known_tags" ] = Json::Value( Json::arrayValue );
	root[ "trash" ] = Json::Value( Json::arrayValue );

	root[ "services" ] = co_await getServiceList( db );

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::addFile( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

template < typename T >
T getDefaultedValue( const std::string name, drogon::HttpRequestPtr request, const T default_value )
{
	return request->getOptionalParameter< T >( name ).value_or( default_value );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::searchFiles( drogon::HttpRequestPtr request )
{
	const auto tags_o { request->getOptionalParameter< std::string >( "tags" ) };
	constexpr auto empty_tags { "[]" };
	const auto tags { tags_o.value_or( empty_tags ) };
	if ( tags == empty_tags )
	{
		Json::Value value;
		value[ "file_ids" ] = Json::Value( Json::arrayValue );

		co_return drogon::HttpResponse::newHttpJsonResponse( value );
	}

	auto db { drogon::app().getDbClient() };

	// Build the search
	SearchBuilder builder {};

	Json::Value tags_json {};
	Json::Reader reader;
	if ( !reader.parse( tags, tags_json ) )
	{
		co_return createBadRequest( "Invalid tags json" );
	}

	std::vector< TagID > tag_ids {};
	for ( const auto& tag : tags_json )
	{
		const auto tag_text { tag.asString() };

		if ( const auto tag_id { co_await findOrCreateTag( tag_text, db ) } )
			tag_ids.push_back( tag_id.value() );
		else
			co_return tag_id.error();
	}

	builder.setTags( tag_ids );

	//TODO: file domains. For now we'll assume all files

	//TODO: Tag service key, Which tag domain to search. Defaults to all tags

	// include_current_tags and include_pending_tags are both things that are not needed for IDHAN so we just skip this.

	const auto file_sort_type {
		static_cast< HydrusSortType >( request->getOptionalParameter< std::uint64_t >( "file_sort_type" )
		                                   .value_or( HydrusSortType::DEFAULT ) )
	};

	const auto file_sort_asc { request->getOptionalParameter< bool >( "file_sort_asc" ).value_or( true ) };

	builder.setSortType( hyToIDHANSortType( file_sort_type ) );
	builder.setSortOrder( file_sort_asc ? SortOrder::ASC : SortOrder::DESC );

	const auto return_file_ids { request->getOptionalParameter< bool >( "return_file_ids" ).value_or( true ) };
	const auto return_hashes { request->getOptionalParameter< bool >( "return_hashes" ).value_or( false ) };
	const auto tag_display_type {
		request->getOptionalParameter< std::string >( "tag_display_type" ).value_or( "display" )
	};

	if ( tag_display_type == std::string_view( "storage" ) )
	{
		builder.setDisplay( HydrusDisplayType::STORED );
	}
	else
	{
		builder.setDisplay( HydrusDisplayType::DISPLAY );
	}

	const auto result { co_await builder.query( db, {} ) };

	Json::Value out {};
	Json::Value file_ids {};
	Json::Value hashes {};
	Json::ArrayIndex i { 0 };

	for ( const auto& row : result )
	{
		if ( return_file_ids ) file_ids[ i ] = row[ "record_id" ].as< RecordID >();
		if ( return_hashes ) hashes[ i ] = SHA256::fromPgCol( row[ "sha256" ] ).hex();

		i += 1;
	}

	if ( return_file_ids ) out[ "file_ids" ] = std::move( file_ids );
	if ( return_hashes ) out[ "hashes" ] = std::move( hashes );

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileHashes( drogon::HttpRequestPtr request )
{
	idhan::fixme();
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	convertQueryRecordIDs( drogon::HttpRequestPtr& request, drogon::orm::DbClientPtr db )
{
	const auto out { co_await helpers::extractRecordIDsFromParameters( request, db ) };
	if ( !out ) co_return std::unexpected( out.error() );

	Json::Value out_json {};

	for ( const auto& id : out.value() ) out_json.append( id );

	request->setParameter( "file_ids", out_json.toStyledString() );

	co_return std::expected< void, drogon::HttpResponsePtr > {};
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::file( const drogon::HttpRequestPtr request )
{
	auto file_id { request->getOptionalParameter< RecordID >( "file_id" ) };
	const auto hash { request->getOptionalParameter< std::string >( "hash" ) };

	if ( hash )
	{
		auto db { drogon::app().getDbClient() };
		const auto sha256 { SHA256::fromHex( hash.value() ) };

		if ( !sha256 ) co_return sha256.error();

		const auto record_result {
			co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256->toVec() )
		};

		if ( record_result.empty() ) co_return createNotFound( "No record with hash {} found", hash.value() );

		file_id = record_result[ 0 ][ "record_id" ].as< RecordID >();
	}

	if ( !file_id && !hash ) co_return createBadRequest( "No hash of file_id specified" );

	const RecordID id { file_id.value() };

	request->setPath( format_ns::format( "/records/{}/file", id ) );

	co_return co_await drogon::app().forwardCoro( request );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::thumbnail( drogon::HttpRequestPtr request )
{
	auto file_id { request->getOptionalParameter< RecordID >( "file_id" ) };
	const auto hash { request->getOptionalParameter< std::string >( "hash" ) };

	RecordID record_id { file_id.value_or( 0 ) };

	if ( hash )
	{
		auto db { drogon::app().getDbClient() };
		const auto sha256 { SHA256::fromHex( hash.value() ) };

		if ( const auto record_id_e { co_await api::helpers::findRecord( *sha256, db ) } )
			record_id = record_id_e.value();
		else
			co_return createNotFound( "No record with hash {} found", hash.value() );
	}

	request->setPath( format_ns::format( "/records/{}/thumbnail", record_id ) );

	co_return co_await drogon::app().forwardCoro( request );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::searchTags( drogon::HttpRequestPtr request )
{
	// http://localhost:16609/hyapi/add_tags/search_tags?search=cat&tag_display_type=display

	const auto search { request->getOptionalParameter< std::string >( "search" ) };
	if ( !search ) co_return createBadRequest( "Must provide search string" );

	const std::string search_value { search.value_or( "" ) };

	// any empty text should return an empty list and 200-OK according to hydrus spec
	if ( search_value == "" )
	{
		Json::Value root;
		root[ "tags" ] = Json::Value( Json::arrayValue );
		co_return drogon::HttpResponse::newHttpJsonResponse( root );
	}

	const auto display_type { request->getOptionalParameter< std::string >( "tag_display_type" ) };

	const std::string display_type_str { display_type.value_or( "storage" ) };
	if ( display_type_str != "storage" && display_type_str != "display" )
	{
		co_return createBadRequest( "Invalid tag display type" );
	}

	// pre-prep the search_value for searching in the database
	const auto db { drogon::app().getDbClient() };

	const auto result { co_await api::getSimilarTags( search_value, db ) };

	co_return drogon::HttpResponse::newHttpJsonResponse( result );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getClientOptions( drogon::HttpRequestPtr request )
{
	//TODO: This. For now i'll just make shit up
	Json::Value json {};

	const std::string RAW_JSON {
		R"({"old_options": {"animation_start_position": 0.0, "confirm_archive": true, "confirm_client_exit": false, "confirm_trash": true, "default_gui_session": "last session", "delete_to_recycle_bin": true, "export_path": ".", "gallery_file_limit": 2000, "hide_preview": false, "hpos": 690, "idle_mouse_period": 600, "idle_normal": true, "idle_period": 1800, "idle_shutdown": 2, "idle_shutdown_max_minutes": 5, "namespace_colours": {"character": [0, 170, 0], "creator": [170, 0, 0], "meta": [0, 0, 0], "person": [0, 128, 0], "series": [170, 0, 170], "studio": [128, 0, 0], "system": [153, 101, 21], "null": [114, 160, 193], "": [0, 111, 250]}, "password": null, "proxy": null, "regex_favourites": [["[1-9]+\\d*(?=.{4}$)\", "\u20260074.jpg -> 74"], ["[^/]+(?=\\s-)\", "E:\\my collection\\author name - v4c1p0074.jpg -> author name"]], "remove_filtered_files": false, "remove_trashed_files": false, "thumbnail_dimensions": [350, 350], "trash_max_age": 72, "trash_max_size": 2048, "vpos": -448}, "options": {"booleans": {"advanced_mode": true, "remove_filtered_files_even_when_skipped": false, "filter_inbox_and_archive_predicates": false, "discord_dnd_fix": false, "secret_discord_dnd_fix": false, "show_unmatched_urls_in_media_viewer": false, "set_search_focus_on_page_change": false, "allow_remove_on_manage_tags_input": true, "yes_no_on_remove_on_manage_tags": true, "activate_window_on_tag_search_page_activation": false, "show_related_tags": true, "show_file_lookup_script_tags": false, "use_native_menubar": false, "shortcuts_merge_non_number_numpad": true, "disable_get_safe_position_test": false, "freeze_message_manager_when_mouse_on_other_monitor": false, "freeze_message_manager_when_main_gui_minimised": false, "load_images_with_pil": false, "only_show_delete_from_all_local_domains_when_filtering": false, "use_system_ffmpeg": true, "elide_page_tab_names": true, "maintain_similar_files_duplicate_pairs_during_idle": true, "show_namespaces": true, "show_number_namespaces": true, "show_subtag_number_namespaces": true, "replace_tag_underscores_with_spaces": false, "replace_tag_emojis_with_boxes": false, "verify_regular_https": true, "page_drop_chase_normally": true, "page_drop_chase_with_shift": false, "page_drag_change_tab_normally": true, "page_drag_change_tab_with_shift": true, "wheel_scrolls_tab_bar": false, "remove_local_domain_moved_files": false, "anchor_and_hide_canvas_drags": false, "touchscreen_canvas_drags_unanchor": false, "import_page_progress_display": true, "process_subs_in_random_order": true, "ac_select_first_with_count": false, "saving_sash_positions_on_exit": true, "database_deferred_delete_maintenance_during_idle": true, "database_deferred_delete_maintenance_during_active": true, "duplicates_auto_resolution_during_idle": true, "duplicates_auto_resolution_during_active": true, "file_maintenance_during_idle": true, "file_maintenance_during_active": true, "tag_display_maintenance_during_idle": true, "tag_display_maintenance_during_active": true, "save_page_sort_on_change": false, "disable_page_tab_dnd": false, "force_hide_page_signal_on_new_page": false, "pause_export_folders_sync": false, "pause_import_folders_sync": true, "pause_repo_sync": false, "pause_subs_sync": false, "pause_all_new_network_traffic": false, "boot_with_network_traffic_paused": false, "pause_all_file_queues": false, "pause_all_watcher_checkers": false, "pause_all_gallery_searches": false, "popup_message_force_min_width": false, "always_show_iso_time": false, "confirm_multiple_local_file_services_move": true, "confirm_multiple_local_file_services_copy": true, "use_advanced_file_deletion_dialog": false, "show_new_on_file_seed_short_summary": false, "show_deleted_on_file_seed_short_summary": false, "only_save_last_session_during_idle": false, "do_human_sort_on_hdd_file_import_paths": true, "highlight_new_watcher": true, "highlight_new_query": true, "delete_files_after_export": false, "file_viewing_statistics_active": true, "file_viewing_statistics_active_on_archive_delete_filter": true, "file_viewing_statistics_active_on_dupe_filter": false, "prefix_hash_when_copying": false, "file_system_waits_on_wakeup": false, "always_show_system_everything": true, "watch_clipboard_for_watcher_urls": false, "watch_clipboard_for_other_recognised_urls": false, "default_search_synchronised": true, "autocomplete_float_main_gui": true, "global_audio_mute": false, "media_viewer_audio_mute": false, "media_viewer_uses_its_own_audio_volume": false, "preview_audio_mute": false, "preview_uses_its_own_audio_volume": true, "always_loop_gifs": true, "always_show_system_tray_icon": true, "minimise_client_to_system_tray": false, "close_client_to_system_tray": false, "start_client_in_system_tray": false, "use_qt_file_dialogs": false, "notify_client_api_cookies": false, "expand_parents_on_storage_taglists": true, "expand_parents_on_storage_autocomplete_taglists": true, "show_parent_decorators_on_storage_taglists": true, "show_parent_decorators_on_storage_autocomplete_taglists": true, "show_sibling_decorators_on_storage_taglists": true, "show_sibling_decorators_on_storage_autocomplete_taglists": true, "show_session_size_warnings": true, "delete_lock_for_archived_files": false, "remember_last_advanced_file_deletion_reason": true, "remember_last_advanced_file_deletion_special_action": false, "do_macos_debug_dialog_menus": true, "save_default_tag_service_tab_on_change": true, "force_animation_scanbar_show": false, "call_mouse_buttons_primary_secondary": false, "start_note_editing_at_end": true, "draw_transparency_checkerboard_media_canvas": false, "draw_transparency_checkerboard_media_canvas_duplicates": true, "menu_choice_buttons_can_mouse_scroll": true, "focus_preview_on_ctrl_click": false, "focus_preview_on_ctrl_click_only_static": false, "focus_preview_on_shift_click": false, "focus_preview_on_shift_click_only_static": false, "focus_media_tab_on_viewer_close_if_possible": false, "fade_sibling_connector": true, "use_custom_sibling_connector_colour": false, "hide_uninteresting_modified_time": true, "draw_tags_hover_in_media_viewer_background": true, "draw_top_hover_in_media_viewer_background": true, "draw_top_right_hover_in_media_viewer_background": true, "draw_notes_hover_in_media_viewer_background": true, "draw_bottom_right_index_in_media_viewer_background": true, "disable_tags_hover_in_media_viewer": false, "disable_top_right_hover_in_media_viewer": false, "media_viewer_window_always_on_top": false, "media_viewer_lock_current_zoom_type": false, "media_viewer_lock_current_zoom": false, "media_viewer_lock_current_pan": false, "allow_blurhash_fallback": true, "fade_thumbnails": true, "slideshow_always_play_duration_media_once_through": false, "enable_truncated_images_pil": true, "do_icc_profile_normalisation": true, "mpv_available_at_start": true, "do_sleep_check": true, "override_stylesheet_colours": true, "command_palette_show_page_of_pages": false, "command_palette_show_main_menu": false, "command_palette_show_media_menu": false, "disallow_media_drags_on_duration_media": false, "show_all_my_files_on_page_chooser": true, "show_local_files_on_page_chooser": false, "use_nice_resolution_strings": true, "use_listbook_for_tag_service_panels": false, "open_files_to_duplicate_filter_uses_all_my_files": true, "show_extended_single_file_info_in_status_bar": true, "hide_duplicates_needs_work_message_when_reasonably_caught_up": true, "file_info_line_consider_archived_interesting": true, "file_info_line_consider_archived_time_interesting": true, "file_info_line_consider_file_services_interesting": false, "file_info_line_consider_file_services_import_times_interesting": false, "file_info_line_consider_trash_time_interesting": false, "file_info_line_consider_trash_reason_interesting": false, "set_requests_ca_bundle_env": false, "mpv_loop_playlist_instead_of_file": false, "draw_thumbnail_rating_background": true, "show_destination_page_when_dnd_url": true, "confirm_non_empty_downloader_page_close": true, "confirm_all_page_closes": false, "refresh_search_page_on_system_limited_sort_changed": true, "do_not_setgeometry_on_an_mpv": false, "mpv_allow_too_many_events_queued": false, "hide_uninteresting_local_import_time": true, "disable_cv_for_gifs": false}, "strings": {"app_display_name": "hydrus client", "namespace_connector": ":", "sibling_connector": " \u2192 ", "or_connector": " OR ", "export_phrase": "{hash}", "current_colourset": "darkmode", "favourite_simple_downloader_formula": "all files linked by images in page", "thumbnail_scroll_rate": "1.0", "pause_character": "\u23f8", "stop_character": "\u23f9", "default_gug_name": "safebooru tag search", "has_audio_label": "\ud83d\udd0a", "has_duration_label": " \u23f5 ", "discord_dnd_filename_pattern": "{hash}", "default_suggested_tags_notebook_page": "related", "last_incremental_tagging_namespace": "page", "last_incremental_tagging_prefix": "", "last_incremental_tagging_suffix": ""}, "noneable_strings": {"favourite_file_lookup_script": "gelbooru md5", "suggested_tags_layout": "notebook", "backup_path": null, "web_browser_path": null, "last_png_export_dir": null, "media_background_bmp_path": null, "http_proxy": null, "https_proxy": null, "no_proxy": "127.0.0.1", "qt_style_name": null, "qt_stylesheet_name": null, "last_advanced_file_deletion_reason": null, "last_advanced_file_deletion_special_action": null, "sibling_connector_custom_namespace_colour": "system", "or_connector_custom_namespace_colour": "system"}, "integers": {"notebook_tab_alignment": 0, "video_buffer_size": 100663296, "related_tags_search_1_duration_ms": 250, "related_tags_search_2_duration_ms": 2000, "related_tags_search_3_duration_ms": 6000, "related_tags_concurrence_threshold_percent": 6, "suggested_tags_width": 300, "similar_files_duplicate_pairs_search_distance": 8, "default_new_page_goes": 3, "close_page_focus_goes": 1, "num_recent_petition_reasons": 5, "max_page_name_chars": 20, "page_file_count_display": 0, "network_timeout": 10, "connection_error_wait_time": 15, "serverside_bandwidth_wait_time": 60, "thumbnail_visibility_scroll_percent": 75, "ideal_tile_dimension": 768, "wake_delay_period": 15, "media_viewer_zoom_center": 2, "last_session_save_period_minutes": 5, "shutdown_work_period": 86400, "max_network_jobs": 15, "max_network_jobs_per_domain": 3, "max_connection_attempts_allowed": 5, "max_request_attempts_allowed_get": 5, "thumbnail_scale_type": 0, "max_simultaneous_subscriptions": 5, "gallery_page_wait_period_pages": 1, "gallery_page_wait_period_subscriptions": 5, "watcher_page_wait_period": 5, "popup_message_character_width": 56, "duplicate_filter_max_batch_size": 250, "video_thumbnail_percentage_in": 2, "global_audio_volume": 80, "media_viewer_audio_volume": 70, "preview_audio_volume": 60, "duplicate_comparison_score_higher_jpeg_quality": 10, "duplicate_comparison_score_much_higher_jpeg_quality": 20, "duplicate_comparison_score_higher_filesize": 10, "duplicate_comparison_score_much_higher_filesize": 20, "duplicate_comparison_score_higher_resolution": 20, "duplicate_comparison_score_much_higher_resolution": 50, "duplicate_comparison_score_more_tags": 8, "duplicate_comparison_score_older": 4, "duplicate_comparison_score_nicer_ratio": 10, "duplicate_comparison_score_has_audio": 20, "thumbnail_cache_size": 2147483648, "image_cache_size": 2147483648, "image_tile_cache_size": 1073741824, "thumbnail_cache_timeout": 86400, "image_cache_timeout": 600, "image_tile_cache_timeout": 300, "image_cache_storage_limit_percentage": 50, "image_cache_prefetch_limit_percentage": 20, "media_viewer_prefetch_delay_base_ms": 1, "media_viewer_prefetch_num_previous": 1, "media_viewer_prefetch_num_next": 6, "thumbnail_border": 1, "thumbnail_margin": 2, "thumbnail_dpr_percent": 100, "file_maintenance_idle_throttle_files": 1, "file_maintenance_idle_throttle_time_delta": 2, "file_maintenance_active_throttle_files": 1, "file_maintenance_active_throttle_time_delta": 20, "subscription_network_error_delay": 43200, "subscription_other_error_delay": 129600, "downloader_network_error_delay": 5400, "file_viewing_stats_menu_display": 2, "number_of_gui_session_backups": 10, "animated_scanbar_height": 20, "animated_scanbar_nub_width": 10, "domain_network_infrastructure_error_number": 3, "domain_network_infrastructure_error_time_delta": 600, "ac_read_list_height_num_chars": 19, "ac_write_list_height_num_chars": 11, "system_busy_cpu_percent": 50, "human_bytes_sig_figs": 3, "ms_to_wait_between_physical_file_deletes": 250, "potential_duplicates_search_work_time_ms": 500, "potential_duplicates_search_rest_percentage": 100, "repository_processing_work_time_ms_very_idle": 30000, "repository_processing_rest_percentage_very_idle": 3, "repository_processing_work_time_ms_idle": 10000, "repository_processing_rest_percentage_idle": 5, "repository_processing_work_time_ms_normal": 500, "repository_processing_rest_percentage_normal": 10, "tag_display_processing_work_time_ms_idle": 15000, "tag_display_processing_rest_percentage_idle": 3, "tag_display_processing_work_time_ms_normal": 100, "tag_display_processing_rest_percentage_normal": 9900, "tag_display_processing_work_time_ms_work_hard": 5000, "tag_display_processing_rest_percentage_work_hard": 5, "deferred_table_delete_work_time_ms_idle": 20000, "deferred_table_delete_rest_percentage_idle": 10, "deferred_table_delete_work_time_ms_normal": 250, "deferred_table_delete_rest_percentage_normal": 1000, "deferred_table_delete_work_time_ms_work_hard": 5000, "deferred_table_delete_rest_percentage_work_hard": 10, "gallery_page_status_update_time_minimum_ms": 1000, "gallery_page_status_update_time_ratio_denominator": 30, "watcher_page_status_update_time_minimum_ms": 1000, "watcher_page_status_update_time_ratio_denominator": 30, "media_viewer_default_zoom_type_override": 0, "preview_default_zoom_type_override": 0, "total_pages_warning": 165}, "noneable_integers": {"forced_search_limit": null, "num_recent_tags": 20, "duplicate_background_switch_intensity_a": 1, "duplicate_background_switch_intensity_b": 3, "last_review_bandwidth_search_distance": 2592000, "file_viewing_statistics_media_min_time_ms": 2000, "file_viewing_statistics_media_max_time_ms": 600000, "file_viewing_statistics_preview_min_time_ms": 5000, "file_viewing_statistics_preview_max_time_ms": 60000, "subscription_file_error_cancel_threshold": 5, "media_viewer_cursor_autohide_time_ms": 700, "idle_mode_client_api_timeout": null, "system_busy_cpu_count": 1, "animated_scanbar_hide_height": 5, "last_backup_time": null, "slideshow_short_duration_loop_percentage": 20, "slideshow_short_duration_loop_seconds": 10, "slideshow_short_duration_cutoff_percentage": 75, "slideshow_long_duration_overspill_percentage": 50, "num_to_show_in_ac_dropdown_children_tab": 40, "number_of_unselected_medias_to_present_tags_for": 4096, "file_viewing_statistics_media_min_time": 2, "file_viewing_statistics_media_max_time": 600, "file_viewing_statistics_preview_min_time": 5, "file_viewing_statistics_preview_max_time": 60, "duplicate_background_switch_intensity": 3}, "keys": {"default_tag_service_tab": "b899f3abb73692b45e09fbb358420400e03594faee78df99182df42b6e4b421c", "default_tag_service_search_page": "616c6c206b6e6f776e2074616773", "default_gug_key": "c52d3b91991f149fb74adf9222ae467bee44d05bf7954e677cb2d63b44474474"}, "colors": {"default": {"0": [255, 255, 255], "1": [217, 242, 255], "2": [32, 32, 36], "3": [64, 64, 72], "4": [223, 227, 230], "5": [1, 17, 26], "6": [248, 208, 204], "7": [227, 66, 52], "8": [255, 255, 255], "9": [235, 248, 255], "10": [255, 255, 255], "11": [0, 0, 0], "12": [255, 255, 255]}, "darkmode": {"0": [64, 64, 72], "1": [112, 128, 144], "2": [64, 13, 2], "3": [171, 39, 79], "4": [145, 163, 176], "5": [223, 227, 230], "6": [248, 208, 204], "7": [227, 66, 52], "8": [52, 52, 52], "9": [83, 98, 103], "10": [52, 52, 52], "11": [112, 128, 144], "12": [35, 38, 41]}}, "media_zooms": [0.01, 0.05, 0.1, 0.15, 0.2, 0.3, 0.5, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.5, 2.0, 3.0, 5.0, 10.0, 20.0], "slideshow_durations": [1.0, 5.0, 10.0, 30.0, 60.0], "default_file_import_options": {"loud": "allowing anything\nexcluding previously deleted\nexcluding gifs > 32 MB\npresenting new files", "quiet": "allowing anything\nexcluding previously deleted\nexcluding gifs > 32 MB\npresenting new files"}, "default_namespace_sorts": [{"sort_metatype": "namespaces", "sort_order": 0, "tag_context": {"service_key": "616c6c206b6e6f776e2074616773", "include_current_tags": true, "include_pending_tags": true, "display_service_key": "616c6c206b6e6f776e2074616773"}, "namespaces": ["series", "creator", "title", "volume", "chapter", "page"], "tag_display_type": 1}, {"sort_metatype": "namespaces", "sort_order": 0, "tag_context": {"service_key": "616c6c206b6e6f776e2074616773", "include_current_tags": true, "include_pending_tags": true, "display_service_key": "616c6c206b6e6f776e2074616773"}, "namespaces": ["creator", "series", "title", "volume", "chapter", "page"], "tag_display_type": 1}], "default_sort": {"sort_metatype": "system", "sort_order": 0, "tag_context": {"service_key": "616c6c206b6e6f776e2074616773", "include_current_tags": true, "include_pending_tags": true, "display_service_key": "616c6c206b6e6f776e2074616773"}, "sort_type": 0}, "default_tag_sort": {"sort_type": 0, "sort_order": 0, "use_siblings": true, "group_by": 0}, "default_tag_sort_search_page": {"sort_type": 0, "sort_order": 0, "use_siblings": true, "group_by": 0}, "default_tag_sort_search_page_manage_tags": {"sort_type": 0, "sort_order": 0, "use_siblings": true, "group_by": 0}, "default_tag_sort_media_viewer": {"sort_type": 0, "sort_order": 0, "use_siblings": true, "group_by": 0}, "default_tag_sort_media_vewier_manage_tags": {"sort_type": 0, "sort_order": 0, "use_siblings": true, "group_by": 0}, "fallback_sort": {"sort_metatype": "system", "sort_order": 0, "tag_context": {"service_key": "616c6c206b6e6f776e2074616773", "include_current_tags": true, "include_pending_tags": true, "display_service_key": "616c6c206b6e6f776e2074616773"}, "sort_type": 2}, "suggested_tags_favourites": {"646f776e6c6f616465722074616773": [], "faa55e36de3876a73402a7f03b162e1eb2bc64e896b63f57b95e181d096742a0": []}, "default_local_location_context": {"current_service_keys": ["6c6f63616c2066696c6573"], "deleted_service_keys": []}}, "services": {"faa55e36de3876a73402a7f03b162e1eb2bc64e896b63f57b95e181d096742a0": {"name": "A.I. Tags", "type": 5, "type_pretty": "local tag service"}, "646f776e6c6f616465722074616773": {"name": "downloader tags", "type": 5, "type_pretty": "local tag service"}, "6c6f63616c2074616773": {"name": "my tags", "type": 5, "type_pretty": "local tag service"}, "b899f3abb73692b45e09fbb358420400e03594faee78df99182df42b6e4b421c": {"name": "public tag repository", "type": 0, "type_pretty": "hydrus tag repository"}, "6c6f63616c2066696c6573": {"name": "my files", "type": 2, "type_pretty": "local file domain"}, "7265706f7369746f72792075706461746573": {"name": "repository updates", "type": 20, "type_pretty": "local update file domain"}, "616c6c206c6f63616c2066696c6573": {"name": "all local files", "type": 15, "type_pretty": "virtual combined local file service"}, "616c6c206c6f63616c206d65646961": {"name": "all my files", "type": 21, "type_pretty": "virtual combined local media service"}, "616c6c206b6e6f776e2066696c6573": {"name": "all known files", "type": 11, "type_pretty": "virtual combined file service"}, "616c6c206b6e6f776e2074616773": {"name": "all known tags", "type": 10, "type_pretty": "virtual combined tag service"}, "6661766f757269746573": {"name": "favourites", "type": 7, "type_pretty": "local like/dislike rating service", "star_shape": "fat star"}, "7472617368": {"name": "trash", "type": 14, "type_pretty": "local trash file domain"}}, "version": 79, "hydrus_version": 617})"
	};

	// turn string into json
	Json::Value root {};

	// For some reason get_client_options needs to also return the service list for whatever reason?

	Json::Reader reader;
	reader.parse( RAW_JSON, root );

	root[ "services" ] = Json::Value( Json::objectValue );

	auto db { drogon::app().getDbClient() };
	root[ "services" ] = co_await getServiceList( db );

	root[ "version" ] = HYDRUS_MIMICED_API_VERSION;
	root[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::hyapi
