#pragma once

#include "conversions.h"

#include <algorithm>
#include <cassert>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <vector>

#include <lsl_cpp.h>

#ifdef XDFZ_SUPPORT
#include <boost/iostreams/filtering_stream.hpp>
using outfile_t = boost::iostreams::filtering_ostream;
#else
#include <fstream>
using outfile_t = std::ofstream;
#endif

using streamid_t = uint32_t;

// the currently defined chunk tags
enum class chunk_tag_t : uint16_t {
	fileheader = 1,   // FileHeader chunk
	streamheader = 2, // StreamHeader chunk
	samples = 3,	  // Samples chunk
	clockoffset = 4,  // ClockOffset chunk
	boundary = 5,	 // Boundary chunk
	streamfooter = 6, // StreamFooter chunk
	undefined = 0
};

// Filetypes, currently support XDF and CSV.
enum class file_type_t : uint16_t {
	xdf = 1, // XDF
	csv = 2, // CSV
};

class LSLStreamWriter {
private:
	outfile_t xdf_file_;
	std::mutex global_file_mutex_;
	bool xdf_created_ = false;
	std::map<streamid_t, outfile_t> data_files_;
	std::map<streamid_t, outfile_t> meta_files_;
	std::map<streamid_t, std::unique_ptr<std::mutex>> file_mutex_;

	std::string filename_;
	file_type_t filetype_;

	outfile_t *_get_file(const streamid_t *streamid_p, chunk_tag_t tag) {
		if (filetype_ == file_type_t::xdf) {
			return &xdf_file_;
		} else {
			if (tag == chunk_tag_t::samples) {
				return &data_files_.at(*streamid_p);
			} else {
				return &meta_files_.at(*streamid_p);
			}
		}
	}

	std::mutex *_get_write_mutex(const streamid_t *streamid_p) {
		if (filetype_ == file_type_t::xdf) {
			return &global_file_mutex_;
		} else {
			std::mutex *inner_mutex;
			{
				std::lock_guard<std::mutex> g_lk(global_file_mutex_);

				auto it = file_mutex_.find(*streamid_p);
				if (it == file_mutex_.end()) {
					it = file_mutex_.emplace(*streamid_p, std::make_unique<std::mutex>()).first;
				}
				inner_mutex = it->second.get();
				return inner_mutex;
			}
		}
	}

	void _write_chunk_header(
		chunk_tag_t tag, std::size_t length, const streamid_t *streamid_p = nullptr);

	// write a generic chunk
	void _write_chunk(
		chunk_tag_t tag, const std::string &content, const streamid_t *streamid_p = nullptr);

public:
	/**
	 * @brief LSLStreamWriter Construct a LSLStreamWriter object
	 * @param filename  Filename to write to
	 */
	LSLStreamWriter(const std::string &filename, file_type_t filetype_ = file_type_t::xdf);

	template <typename T>
	void write_data_chunk(streamid_t streamid, const std::vector<double> &timestamps,
		const std::vector<T> &chunk, uint32_t n_samples, uint32_t n_channels);
	template <typename T>
	void write_data_chunk(streamid_t streamid, const std::vector<double> &timestamps,
		const std::vector<T> &chunk, uint32_t n_channels) {
		assert(timestamps.size() * n_channels == chunk.size());
		write_data_chunk(streamid, timestamps, chunk, timestamps.size(), n_channels);
	}
	template <typename T>
	void write_data_chunk_nested(streamid_t streamid, const std::vector<double> &timestamps,
		const std::vector<std::vector<T>> &chunk);

	/**
	 * @brief init_stream Ensures a file is available for the referenced stream.
	 * For XDF recordings, this can be called multiple times but only one file is created.
	 * For CSV recordings, this will create a meta file and csv data file for the given stream.
	 * @param streamid Numeric stream identifier
	 * @param content XML-formatted stream header
	 */
	void init_stream_file(streamid_t streamid, std::string stream_name = "");

	/**
	 * @brief write_stream_header Write the stream header, see also
	 * @see https://github.com/sccn/xdf/wiki/Specifications#clockoffset-chunk
	 * @param streamid Numeric stream identifier
	 * @param content XML-formatted stream header
	 */
	void write_stream_header(streamid_t streamid, const std::string &content);
	/**
	 * @brief write_stream_footer
	 * @see https://github.com/sccn/xdf/wiki/Specifications#streamfooter-chunk
	 */
	void write_stream_footer(streamid_t streamid, const std::string &content);
	/**
	 * @brief write_stream_offset Record the time discrepancy between the
	 * streaming and the recording PC
	 * @see https://github.com/sccn/xdf/wiki/Specifications#clockoffset-chunk
	 */
	void write_stream_offset(streamid_t streamid, double collectiontime, double offset);
	/**
	 * @brief write_boundary_chunk Insert a boundary chunk that's mostly used
	 * to recover from errors in XDF files by providing a restart marker.
	 */
	void write_boundary_chunk();
};

inline void write_ts(std::ostream &out, double ts) {
	// Write timestamp.
	if (ts == 0)
		out.put(0);
	else {
		// [TimeStampBytes].
		out.put(8);
		// [TimeStamp].
		write_little_endian(out, ts);
	}
}

template <typename T>
void LSLStreamWriter::write_data_chunk(streamid_t streamid, const std::vector<double> &timestamps,
	const std::vector<T> &chunk, uint32_t n_samples, uint32_t n_channels) {

	/**
		Samples data chunk: [Tag 3] [VLA ChunkLen] [StreamID] [VLA NumSamples]
		[NumSamples x [VLA TimestampLen] [TimeStampLen]
		[NumSamples x NumChannels Sample]
	**/
	if (n_samples == 0) return;
	if (timestamps.size() != n_samples)
		throw std::runtime_error("timestamp / sample count mismatch");

	// Generate [Samples] chunk contents...

	std::ostringstream out;
	std::string outstr;

	// XDF formatter.
	if (filetype_ == file_type_t::xdf) {
		auto raw_data = chunk.data();

		write_fixlen_int(out, 0x0FFFFFFF); // Placeholder length, will be replaced later.
		for (double ts : timestamps) {
			write_ts(out, ts);
			// Write sample, get the current position in the chunk array back.
			raw_data = write_sample_values(out, raw_data, n_channels);
		}
		outstr = std::string(out.str());
		// Replace length placeholder.
		auto s = static_cast<uint32_t>(n_samples);
		std::copy(
			reinterpret_cast<char *>(&s), reinterpret_cast<char *>(&s + 1), outstr.begin() + 1);
	}

	// CSV formatter.
	else if (filetype_ == file_type_t::csv) {
		for (double ts : timestamps) {
			// Write sample, get the current position in the chunk array back.
			// raw_data = write_sample_values(out, raw_data, n_channels);
		}
		outstr = std::string("hello");
	}

	std::lock_guard<std::mutex> lock(*_get_write_mutex(&streamid));
	_write_chunk(chunk_tag_t::samples, outstr, &streamid);
}

template <typename T>
void LSLStreamWriter::write_data_chunk_nested(streamid_t streamid,
	const std::vector<double> &timestamps, const std::vector<std::vector<T>> &chunk) {
	if (chunk.size() == 0) return;
	auto n_samples = timestamps.size();
	if (timestamps.size() != chunk.size())
		throw std::runtime_error("timestamp / sample count mismatch");
	auto n_channels = chunk[0].size();

	// Generate [Samples] chunk contents...

	std::ostringstream out;
	std::string outstr;

	// XDF formatter.
	if (filetype_ == file_type_t::xdf) {
		write_fixlen_int(out, 0x0FFFFFFF); // Placeholder length, will be replaced later.
		auto sample_it = chunk.cbegin();
		for (double ts : timestamps) {
			assert(n_channels == sample_it->size());
			write_ts(out, ts);
			// Write sample, get the current position in the chunk array back.
			write_sample_values(out, sample_it->data(), n_channels);
			sample_it++;
		}
		outstr(out.str());
		// Replace length placeholder.
		auto s = static_cast<uint32_t>(n_samples);
		std::copy(
			reinterpret_cast<char *>(&s), reinterpret_cast<char *>(&s + 1), outstr.begin() + 1);
	}

	// CSV formatter.
	else if (filetype_ == file_type_t::csv) {
		outstr(out.str());
	}

	std::lock_guard<std::mutex> lock(*_get_write_mutex(&streamid));
	_write_chunk(chunk_tag_t::samples, outstr, &streamid);
}
