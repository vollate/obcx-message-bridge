add_library(
  bridge_core STATIC
  config.cpp
  bridge_bot_failures.cpp
  bridge_command_operations.cpp
  bridge_delivery_sender.cpp
  bridge_delivery_revision.cpp
  bridge_delivery_revision_retry.cpp
  bridge_delivery_revision_mutations.cpp
  bridge_delivery_retry.cpp
  bridge_delivery_publication.cpp
  bridge_delivery_retry_worker.cpp
  bridge_media_reader.cpp
	  bridge_forwarding_runtime.cpp
	  bridge_hash.cpp
	  bridge_notice_event_adapter.cpp
  bridge_state_repository.cpp
  bridge_status.cpp
  received_message_repository.cpp
  media_converter.cpp
  media_processor.cpp
  path_manager.cpp
  # qq
  qq/command_handler.cpp
  qq/event_handler.cpp
  qq/handler.cpp
  qq/text_forwarder.cpp
  qq/image_preparer.cpp
  qq/voice_preparer.cpp
  qq/voice_normalizer.cpp
  qq/forward_preparer.cpp
  # QQ ordinary forwarding is owned by TextForwarder and its typed preparers.
  # miniapp.cpp implements the still-active CardFormatter, not QQMediaProcessor.
  qq/media_processor/miniapp.cpp
  qq/photo_normalizer.cpp
  # telegram
  telegram/ordinary_forwarder.cpp
  telegram/album_forwarder.cpp
  telegram/album_delivery_worker.cpp
  telegram/edit_preparer.cpp
  telegram/command_handler.cpp
  telegram/reaction_emoji.cpp
  telegram/media_processor/animation.cpp
  telegram/media_processor/cache.cpp
  telegram/media_processor/dispatch.cpp
  telegram/media_processor/sticker.cpp
  telegram/media_group_buffer.cpp
  telegram/command_facade.cpp)

obcx_package_target(bridge_core ROLE implementation)

# Include decoder objects in the actor's implementation archive: no runtime
# shared-library lookup or external SILK executable is required.
target_sources(bridge_core PRIVATE $<TARGET_OBJECTS:bridge_silk>)
target_include_directories(bridge_core PRIVATE
  ../third_party/silk/interface ../third_party/silk/src)
