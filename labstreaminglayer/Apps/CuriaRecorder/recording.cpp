#include "recording.h"
#include <optional>
//#include "conversions.h"
#include <regex>
#include <set>
#include <sstream>
#ifdef XDFZ_SUPPORT
#include <boost/algorithm/string/predicate.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#endif

// Thread utilities
using Clock = std::chrono::high_resolution_clock;

/**
 * @brief try_join_once		joins and deconstructs the thread if possible
 * @param thread			unique_ptr to a std::tread. Will be reset on success
 * @return					true if the thread was successfully joined, false otherwise
 */
inline bool try_join_once(std::unique_ptr<std::thread> &thread) {
	if (thread && thread->joinable()) {
		thread->join();
		thread.reset();
		return true;
	}
	return false;
}

/**
 * @brief timed_join	Tries to join the passed thread until it succeeds or duration passes
 * @param thread		unique_ptr to a std::tread. Will be reset on success
 * @param duration		max duration to try joining
 * @return true on success, false otherwise
 */
inline bool timed_join(thread_p &thread, std::chrono::milliseconds duration = max_join_wait) {
	const auto start = Clock::now();
	while (Clock::now() - start < duration) {
		if (try_join_once(thread)) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	return false;
}

/**
 * @brief timed_join_or_detach	Join the thread or detach it if not possible within specified
 * duration
 * @param thread				unique_ptr to a std::tread. Will be reset on success
 * @param duration max			duration to try joining
 */
inline void timed_join_or_detach(
	thread_p &thread, std::chrono::milliseconds duration = max_join_wait) {
	if (!timed_join(thread, duration)) {
		thread->detach();
		std::cerr << "Thread didn't join in time!" << std::endl;
	}
}

/**
 * @brief timed_join_or_detach	Join the thread or detach it if not possible within specified
 * duration
 * @param threads				list of unique_ptrs to std::threads. Guaranteed to be empty
 * afterwards.
 * @param duration				duration to try joining
 */
inline void timed_join_or_detach(
	std::list<thread_p> &threads, std::chrono::milliseconds duration = max_join_wait) {
	const auto start = Clock::now();
	while (Clock::now() - start < duration && !threads.empty()) {
		for (auto it = threads.begin(); it != threads.end();) {
			if (try_join_once(*it))
				it = threads.erase(it);
			else
				++it;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	if (!threads.empty()) {
		std::cerr << threads.size() << " stream threads still running!" << std::endl;
		for (auto &t : threads) t->detach();
		threads.clear();
	}
}

recording::recording(
	const std::string &filename, 
	file_type_t filetype,
	const std::vector<lsl::stream_info> &streams,
	const std::vector<std::string> &watchfor,
	std::map<std::string, int> sync_options,
	int sync_default,
	bool collect_offsets,
	bool recording_timestamps,
	std::chrono::milliseconds chunk_interval)
	: file_(filename, filetype), 
	  unsorted_(false), streamid_(0),
	  shutdown_(false), headers_to_finish_(0),
	  streaming_to_finish_(0),
	  sync_options_by_stream_(std::move(sync_options)),
	  sync_default_(sync_default),
	  offsets_enabled_(collect_offsets),
	  recording_timestamps_enabled_(recording_timestamps),
	  chunk_interval_(chunk_interval) {
	// create a recording thread for each stream
	for (const auto &stream : streams)
		stream_threads_.emplace_back(
			new std::thread(&recording::record_from_streaminfo, this, stream, true));
	// create a resolve-and-record thread for each item in the watchlist
	for (const auto &thread : watchfor)
		stream_threads_.emplace_back(
			new std::thread(&recording::record_from_query_results, this, thread));
	// create a boundary chunk writer thread
	boundary_thread_ = std::make_unique<std::thread>(&recording::record_boundaries, this);
}

recording::~recording() {
	try {
		// set the shutdown flag (from now on no more new streams)
		shutdown_ = true;

		// stop the threads
		timed_join_or_detach(stream_threads_, max_join_wait);
		if (!timed_join(boundary_thread_, max_join_wait + boundary_interval)) {
			safe_print_error("boundary_thread didn't finish in time!");
			boundary_thread_->detach();
		}
		safe_print("Closing the file(s).");
	} catch (std::exception &e) {
		safe_print_error("Error while closing the recording: " + std::string(e.what()));
	}
}

void recording::record_from_query_results(const std::string &query) {
	try {
		std::set<std::string> known_uids;		// set of previously seen stream uid's
		std::set<std::string> known_source_ids; // set of previously seen source id's
		std::list<thread_p> threads;			// our spawned threads
		
		safe_print("Watching for a stream with properties " + query);
		while (!shutdown_) {
			// periodically re-resolve the query
			std::vector<lsl::stream_info> results = lsl::resolve_stream(query, 0, resolve_interval);
			// for each result...
			for (auto &result : results) {
				// if it is a new stream...
				if (!known_uids.count(result.uid()))
					// and doesn't have a previously seen source id...
					if (!(!result.source_id().empty() &&
							(!known_source_ids.count(result.source_id())))) {
						safe_print("Found a new stream named " + result.name() + ", adding it to the recording.");
						// start a new recording thread
						threads.emplace_back(new std::thread(
							&recording::record_from_streaminfo, this, result, false));
						// ... and add it to the lists of known id's
						known_uids.insert(result.uid());
						if (!result.source_id().empty())
							known_source_ids.insert(result.source_id());
					}
			}
		}
		// wait for all our threads to join
		timed_join_or_detach(threads, max_join_wait);
	} catch (std::exception &e) {
		safe_print_error("Error in the record_from_query_results thread: " + std::string(e.what()));
	}
}

void recording::record_from_streaminfo(const lsl::stream_info &src, bool phase_locked) {
	try {
		double first_timestamp, last_timestamp;
		uint64_t sample_count = 0;
		// obtain a fresh streamid
		streamid_t streamid = fresh_streamid();

		inlet_p in;
		lsl::stream_info info;

		// --- headers phase
		try {
			enter_headers_phase(phase_locked);

			// open an inlet to read from (and subscribe to data immediately)
			in.reset(new lsl::stream_inlet(src));
			auto it = sync_options_by_stream_.find(src.name() + " (" + src.hostname() + ")");
			try {
				if (it != sync_options_by_stream_.end())
					in->set_postprocessing(it->second);
				else if (sync_default_ > -1) {
					in->set_postprocessing(sync_default_);
				}
			} catch (std::invalid_argument &ex) {
				safe_print_error("Set post processing failed for stream " + std::to_string(streamid) + ". Check your provided flags value.");
			}

			try {
				in->open_stream(max_open_wait);
				safe_print("Opened the stream " + src.name() + ".");
			} catch (lsl::timeout_error &ex) {
				safe_print_error(
					"Subscribing to the stream " + src.name() +
					" is taking relatively long; collection from this stream will be delayed.");
			}

			// retrieve the stream header & get its XML version
			info = in->info();
			std::string stream_meta_data = info.as_xml();
			file_.init_stream_file(streamid, info.name()); // Ensures we create enough files for
														   // each stream (in the case of CSVs).
			if (recording_timestamps_enabled_) {
				// Inject 1 or 2 new channels to hold Unix recording timestamp for double, float,
				// int, and string streams.
				int added_channels = 0;
				switch (src.channel_format()) {
				case lsl::cf_int32:
					stream_meta_data = std::regex_replace(stream_meta_data,
						std::regex(recording_timestamp_replace_node),
						recording_timestamp_int32_channel_info);
					added_channels = 2;
					break;
				case lsl::cf_float32:
					stream_meta_data = std::regex_replace(stream_meta_data,
						std::regex(recording_timestamp_replace_node),
						recording_timestamp_float32_channel_info);
					added_channels = 2;
					break;
				case lsl::cf_double64:
					stream_meta_data = std::regex_replace(stream_meta_data,
						std::regex(recording_timestamp_replace_node),
						recording_timestamp_double_string_channel_info);
					added_channels = 1;
					break;
				case lsl::cf_string:
					stream_meta_data = std::regex_replace(stream_meta_data,
						std::regex(recording_timestamp_replace_node),
						recording_timestamp_double_string_channel_info);
					added_channels = 1;
					break;
				}
				int channel_count = src.channel_count();
				stream_meta_data = std::regex_replace(stream_meta_data,
					std::regex("<channel_count>" + std::to_string(channel_count)),
					"<channel_count>" + std::to_string(channel_count + added_channels));
			}

			file_.write_stream_header(streamid, stream_meta_data, in->get_channel_count() + (recording_timestamps_enabled_ ? 1 : 0));
			safe_print("Received header for stream " + src.name() + ".");

			leave_headers_phase(phase_locked);
		} catch (std::exception &) {
			leave_headers_phase(phase_locked);
			throw;
		}

		// --- streaming phase
		try {
			// this waits until we are done writing all headers for the initial set of
			// (phase-locked) streams (any streams that are discovered later, if any, will not wait)
			// we're doing this so that all headers of the initial set of streams come first, so the
			// XDF file is properly sorted unless we discover some streams later which someone
			// "forgot to turn on" before the recording started; in that case the file would have to
			// be post-processed to be in properly sorted (seekable) format
			enter_streaming_phase(phase_locked);
			safe_print("Started data collection for stream " + src.name() + ".");

			// now write the actual sample chunks...
			switch (src.channel_format()) {
			case lsl::cf_int8:
				typed_transfer_loop<char>(streamid, info.nominal_srate(), in, first_timestamp,
					last_timestamp, sample_count);
				break;
			case lsl::cf_int16:
				typed_transfer_loop<int16_t>(streamid, info.nominal_srate(), in, first_timestamp,
					last_timestamp, sample_count);
				break;
			case lsl::cf_int32:
				typed_transfer_loop<int32_t>(streamid, info.nominal_srate(), in, first_timestamp,
					last_timestamp, sample_count);
				break;
			case lsl::cf_float32:
				typed_transfer_loop<float>(streamid, info.nominal_srate(), in, first_timestamp,
					last_timestamp, sample_count);
				break;
			case lsl::cf_double64:
				typed_transfer_loop<double>(streamid, info.nominal_srate(), in, first_timestamp,
					last_timestamp, sample_count);
				break;
			case lsl::cf_string:
				typed_transfer_loop<std::string>(streamid, info.nominal_srate(), in,
					first_timestamp, last_timestamp, sample_count);
				break;
			default:
				// unsupported channel format
				throw std::runtime_error(
					std::string("Unsupported channel format in stream ") += src.name());
			}

			leave_streaming_phase(phase_locked);
		} catch (std::exception &) {
			leave_streaming_phase(phase_locked);
			throw;
		}

		// --- footers phase
		try {
			enter_footers_phase(phase_locked);

			// now generate the [StreamFooter] contents
			std::ostringstream footer;
			footer.precision(16);
			// [Content]
			footer << "<?xml version=\"1.0\"?><info><first_timestamp>" << first_timestamp
				   << "</first_timestamp><last_timestamp>" << last_timestamp
				   << "</last_timestamp><sample_count>" << sample_count << "</sample_count>";
			footer << "<clock_offsets>";
			{
				// including the clock_offset list
				std::lock_guard<std::mutex> lock(offset_mut_);
				for (const auto pair : offset_lists_[streamid]) {
					footer << "<offset><time>" << pair.first << "</time><value>" << pair.second
						   << "</value></offset>";
				}
				footer << "</clock_offsets></info>";
			}
			file_.write_stream_footer(streamid, footer.str());

			safe_print("Wrote footer for stream " + src.name() + ".");
			leave_footers_phase(phase_locked);
		} catch (std::exception &) {
			leave_footers_phase(phase_locked);
			throw;
		}
	} catch (std::exception &e) {
		safe_print_error("Error in the record_from_streaminfo thread: " + std::string(e.what()));
	}
}

void recording::record_boundaries() {
	try {
		auto next_boundary = Clock::now() + boundary_interval;
		while (!shutdown_) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (Clock::now() > next_boundary) {
				file_.write_boundary_chunk();
				next_boundary = Clock::now() + boundary_interval;
			}
		}
	} catch (std::exception &e) {
		safe_print_error(std::string("Error in the record_boundaries thread: ") + e.what());
	}
}

void recording::record_offsets(
	streamid_t streamid, const inlet_p &in, std::atomic<bool> &offset_shutdown) noexcept {
	try {
		while (!shutdown_ && !offset_shutdown) {
			// Sleep for the interval.
			std::this_thread::sleep_for(offset_interval);
			// Query the time offset.
			double offset = std::numeric_limits<double>::infinity();
			double now = lsl::local_clock();
			try {
				offset = in->time_correction(2.5);
			} catch (lsl::timeout_error &) {
				safe_print_error("Timeout in time correction query for stream " + streamid);
			}
			file_.write_stream_offset(streamid, now, offset);
			// Also append to the offset lists.
			std::lock_guard<std::mutex> lock(offset_mut_);
			offset_lists_[streamid].emplace_back(now - offset, offset);
		}
	} catch (std::exception &e) { 
		safe_print_error(std::string("Error in the record_offsets thread: ") + e.what());
	}
	safe_print("Offsets thread is finished.");
}

void recording::enter_headers_phase(bool phase_locked) {
	if (phase_locked) {
		std::lock_guard<std::mutex> lock(phase_mut_);
		headers_to_finish_++;
	}
}

void recording::leave_headers_phase(bool phase_locked) {
	if (phase_locked) {
		std::unique_lock<std::mutex> lock(phase_mut_);
		headers_to_finish_--;
		lock.unlock();
		ready_for_streaming_.notify_all();
	}
}

void recording::enter_streaming_phase(bool phase_locked) {
	if (phase_locked) {
		std::unique_lock<std::mutex> lock(phase_mut_);
		ready_for_streaming_.wait_for(
			lock, max_headers_wait, [this]() { return this->ready_for_streaming(); });
		streaming_to_finish_++;
	}
}

void recording::leave_streaming_phase(bool phase_locked) {
	if (phase_locked) {
		std::unique_lock<std::mutex> lock(phase_mut_);
		streaming_to_finish_--;
		lock.unlock();
		ready_for_footers_.notify_all();
	}
}

void recording::enter_footers_phase(bool phase_locked) {
	if (phase_locked) {
		std::unique_lock<std::mutex> lock(phase_mut_);
		ready_for_footers_.wait_for(
			lock, max_footers_wait, [this]() { return this->ready_for_footers(); });
	}
}

template <class T>
void recording::typed_transfer_loop(streamid_t streamid, double srate, const inlet_p &in,
	double &first_timestamp, double &last_timestamp, uint64_t &sample_count) {
	// optionally start an offset collection thread for this stream
	std::atomic<bool> offset_shutdown{false};
	thread_p offset_thread(offsets_enabled_ ? new std::thread(&recording::record_offsets, this,
												  streamid, in, std::ref(offset_shutdown))
											: nullptr);
	try {
		double sample_interval = srate ? 1.0 / srate : 0;

		// temporary data
		std::vector<T> chunk;
		std::vector<double> timestamps;
		int channelCount = 0;
		double lsl_timeout = chunk_interval_.count() / 1000.00;

		// Pull the first sample.
		first_timestamp = last_timestamp = in->pull_sample(chunk, lsl_timeout);
		timestamps.push_back(first_timestamp);
		channelCount = in->get_channel_count();

		if (recording_timestamps_enabled_) {
			inject_recording_timestamps_(&chunk, channelCount, timestamps.size());
		}

		file_.write_data_chunk(streamid, timestamps, chunk, channelCount);

		while (!shutdown_) {
			// Get a chunk from the stream.
			in->pull_chunk_multiplexed(chunk, &timestamps, lsl_timeout);
			// For each sample...
			for (double &ts : timestamps) {
				// If the time stamp can be deduced from the previous one...
				if (last_timestamp + sample_interval == ts) {
					last_timestamp = ts + sample_interval;
				} else {
					last_timestamp = ts;
				}
			}
			channelCount = in->get_channel_count();
			if (recording_timestamps_enabled_) {
				inject_recording_timestamps_(&chunk, channelCount, timestamps.size());
			}
			// Write the actual chunk.
			file_.write_data_chunk(streamid, timestamps, chunk, channelCount);
			sample_count += timestamps.size();

			std::this_thread::sleep_for(chunk_interval_);
		}
	} catch (std::exception &e) {
		safe_print_error(std::string("Error in transfer thread: ") + e.what());
		offset_shutdown = true;
		if (offsets_enabled_) { timed_join_or_detach(offset_thread); }
		throw;
	}
	if (offsets_enabled_) { timed_join_or_detach(offset_thread); }
}
