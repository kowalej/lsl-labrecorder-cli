#include "LSLStreamWriter.h"
#include "process.h"
#include "recording.h"
#include <QCommandLineParser>
#include <Windows.h>
#include <conio.h>
#include <csignal>
#include <iostream>
#include <time.h>

#define TIMEOUT_DEFAULT 5
#define TIMEOUT_DEFAULT_STR "5"

#define RESOLVE_TIMEOUT_DEFAULT 1
#define RESOLVE_TIMEOUT_DEFAULT_STR "1"

#define POST_PROCESSING_DEFAULT -1
#define POST_PROCESSING_DEFAULT_STR "-1"

#define CHUNK_INTERVAL_DEFAULT 500
#define CHUNK_INTERVAL_DEFAULT_STR "500"

#define EMPTY_PLACEHOLDER " "

bool NOEXIT = true;

void exitHandler(int signum) {
	std::cout << "Exit signal recieved, shutting down.\n";
	NOEXIT = false;
}

bool find_streams(
	QString query, double timeout, double resolve_timeout, std::vector<lsl::stream_info> &streams) {
	query =
		query.replace("\"", "'"); // Double quotes should be single quotes for LSL query to work.

	bool match = false;
	clock_t start = clock();
	std::vector<lsl::stream_info> all_streams;

	std::cout << "\nSearching for streams..." << std::endl;

	signal(SIGINT, exitHandler); // Check for Ctrl + C hit to cancel.
	while (NOEXIT) {
		if (all_streams.size() > 0 || ((clock() - start) / CLOCKS_PER_SEC) >= timeout) { break; }
		all_streams = lsl::resolve_streams(resolve_timeout);
	}

	for (const auto &info : all_streams) {
		if (query == "*" || info.matches_query(query.toStdString().c_str())) {
			streams.emplace_back(info);
			match = true;
		}
	}
	return match;
}

void display_stream_info(
	std::vector<lsl::stream_info> &streams, bool matches, QString query, bool verbose = false) {
	bool query_mode = !query.isEmpty();

	// Extra line before info.
	std::cout << std::endl;

	if (matches) {
		// Query matched is for find command, list command uses found.
		std::string preamble = query_mode ? "Query matched " : "Found ";
		std::cout << preamble << streams.size() << " stream" << (streams.size() > 1 ? "s:" : ":")
				  << std::endl;
		int index = 1;
		for (auto &stream : streams) {
			std::cout << "  " << index << ". " << stream.name() << " @ " << stream.hostname()
					  << std::endl;
			if (verbose) { std::cout << stream.as_xml() << std::endl; }
			index++;
		}
		std::cout << std::endl;
	} else {
		if (query_mode) {
			std::cout << "Query \"" << query.toStdString() << "\" did not match any streams."
					  << std::endl;
		} else {
			std::cout << "No streams were found." << std::endl;
		}
		std::cout << std::endl;
	}
}

int execute_list_command(double timeout, double resolve_timeout, bool verbose) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams("*", timeout, resolve_timeout, streams);
	display_stream_info(streams, matches, "", verbose);

	return matches ? 0 : 2;
}

int execute_find_command(QString query, double timeout, double resolve_timeout, bool verbose) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams(query, timeout, resolve_timeout, streams);
	display_stream_info(streams, matches, query, verbose);

	return matches ? 0 : 2;
}

int execute_record_command(QString query, QString filename, file_type_t file_type, double timeout,
	double resolve_timeout, bool collect_offsets, bool recording_timestamps,
	int post_processing_flag, std::chrono::milliseconds chunk_interval) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams(query, timeout, resolve_timeout, streams);
	display_stream_info(streams, matches, query);

	// End command if no matches found.
	if (!matches) { return 2; }


	std::vector<std::string> watchfor;
	std::map<std::string, int>
		sync_options; // Per stream sync options (post processing) not yet supported.
	std::cout << "-------------------------------------------------------" << std::endl;
	std::cout << "--- Starting the recording, press Ctrl+C to quit... ---" << std::endl;
	std::cout << "-------------------------------------------------------" << std::endl;
	recording r(filename.toStdString(), file_type, streams, watchfor, sync_options,
		post_processing_flag, collect_offsets, recording_timestamps, chunk_interval);
	signal(SIGINT, exitHandler); // Check for Ctrl + C hit to cancel.
	while (NOEXIT) { std::this_thread::sleep_for(std::chrono::milliseconds(1000)); }
	return 0;
}

void incorrect_usage(QCommandLineParser &parser, std::string message, bool show_help = false) {
	if (show_help) {
		std::cout << message << ". Pass in -h or --help for more info." << std::endl;
	} else {
		std::cout << message << "." << std::endl;
	}
	exit(2);
}

void invalid_arg(QString name) {
	std::cout << "Invalid value provided for argument: " << name.toStdString() << "." << std::endl;
	exit(2);
}

double parse_timeout(QString timeout_str, QStringList option_names) {
	try {
		return timeout_str.isEmpty() ? TIMEOUT_DEFAULT : std::stod(timeout_str.toStdString());
	} 
	catch (std::invalid_argument) {} catch (std::out_of_range) {}
	invalid_arg(option_names.join(", "));
}

double parse_resolve_timeout(QString resolve_timeout_str, QStringList option_names) {
	try {
		return resolve_timeout_str.isEmpty() ? RESOLVE_TIMEOUT_DEFAULT
											 : std::stod(resolve_timeout_str.toStdString());
	}
	catch (std::invalid_argument) {}
	catch (std::out_of_range) {}
	invalid_arg(option_names.join(", "));
}

int parse_post_processing(QString post_processing_str, QStringList option_names) {
	try {
	return post_processing_str.isEmpty() ? POST_PROCESSING_DEFAULT
										 : std::stoi(post_processing_str.toStdString());
	}
	catch (std::invalid_argument) {}
	catch (std::out_of_range) {}
	invalid_arg(option_names.join(", "));
}

std::chrono::milliseconds parse_chunk_interval(QString chunk_interval_str, QStringList option_names) {
	try {
	return chunk_interval_str.isEmpty()
			   ? std::chrono::milliseconds(CHUNK_INTERVAL_DEFAULT)
			   : std::chrono::milliseconds(std::stoi(chunk_interval_str.toStdString()));
	}
	catch (std::invalid_argument) {}
	catch (std::out_of_range) {}
	invalid_arg(option_names.join(", "));
}

void process_command(QCommandLineParser &parser, QCoreApplication &app, QStringList &pos_args,
	int expected_num_pos_args = 0) {
	// Process args.
	parser.process(app);
	parser.process(QCoreApplication::arguments());
	if (parser.isSet("help")) {
		// Remove initial [options] tag since we are running subcommand,
		// and remove extra space caused by dummy option.
		auto help_text =
			parser.helpText().replace(" [options]", "").replace("Arguments:\n", "Arguments:");
		if (expected_num_pos_args == 0) {
			// Remove empty arguments and extra space.
			help_text = help_text.replace("Arguments:", "");
			help_text.truncate(help_text.lastIndexOf("\n"));
			help_text.truncate(help_text.lastIndexOf("\n"));
		}
		std::cout << help_text.toStdString() << std::endl;
		exit(0);
	}
	pos_args = parser.positionalArguments();
	int num_pos_args = pos_args.count() - 1;
	if (num_pos_args < expected_num_pos_args) {
		std::stringstream msg;
		msg << "Incorrect number of arguments provided (got " << num_pos_args << ", expected "
			<< expected_num_pos_args << ")";
		incorrect_usage(parser, msg.str());
	}
}

int main(int argc, char **argv) {
#pragma region Basic app setup
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("Curia Recorder");
	QCoreApplication::setApplicationVersion("1.0");

#pragma endregion

#pragma region Process the CLI options
	// Main parser which derives subcommand.
	QCommandLineParser parser;
	parser.setApplicationDescription("\nRecord and discover LSL streams.");
	parser.addHelpOption();
	parser.addVersionOption();
	parser.setOptionsAfterPositionalArgumentsMode(
		QCommandLineParser::OptionsAfterPositionalArgumentsMode::ParseAsOptions);

	parser.addPositionalArgument("command", "The command to execute. See commands below:\n"
											"--------------------------------------------\n"
											"record - Start an LSL recording.\n"
											"list - List all LSL streams.\n"
											"find - Find LSL streams via query.\n");

	// Command parser which processes the subcommand.
	QCommandLineParser commandParser;
	QCommandLineOption custom_help_option(QStringList() << "?"
														<< "h"
														<< "help",
		"Displays this help.");
	commandParser.addOption(custom_help_option);

	commandParser.setOptionsAfterPositionalArgumentsMode(
		QCommandLineParser::OptionsAfterPositionalArgumentsMode::ParseAsOptions);

	// Timeout option (-t, --timeout). Default value = 5.
	QCommandLineOption timeout_option(QStringList() << "t"
													<< "timeout",
		"Maximum overall time (in seconds) to wait while searching for stream(s). Default "
		"= " TIMEOUT_DEFAULT_STR ".",
		"seconds", QString(TIMEOUT_DEFAULT_STR));

	// Resolve timeout option (-l, --lsl-resolve-timeout). Default value = 1.
	QCommandLineOption resolve_timeout_option(QStringList() << "l"
															<< "lsl-resolve-timeout",
		"Time (in seconds) to wait during each LSL call to resolve stream(s). Default "
		"= " RESOLVE_TIMEOUT_DEFAULT_STR ".",
		"seconds", QString(RESOLVE_TIMEOUT_DEFAULT_STR));

	// Verbose data option (-d, --detailed) to show all stream XML data.
	QCommandLineOption verbose_option(QStringList() << "x"
													<< "xml",
		"Show verbose stream data as XML.");

	// Collect offsets option (-o, --offsets).
	QCommandLineOption collect_offsets_option(QStringList() << "o"
															<< "offsets",
		"Set this flag to collect offsets in the stream.");

	// Recording timestamps option (-r, --recording-timestamps).
	QCommandLineOption recording_timestamps_option(QStringList() << "r"
																 << "recording-timestamps",
		"Add (as an LSL channel) a timestamp indicating when the sample was recorded.");

	// Post processing flag option (-p, --post-flag).
	QCommandLineOption post_processing_option(QStringList() << "p"
															<< "post-process",
		"Post processing flags (i.e. online sync options). Defaults to no post-processing. See "
		"docs for details.",
		"int", QString(POST_PROCESSING_DEFAULT_STR));

	// Chunk interval flag option (-c, --chunk-interval).
	QCommandLineOption chunk_interval_option(QStringList() << "c"
														   << "chunk-interval",
		"Time (in milliseconds) to wait between pulling LSL chunks. Default "
		"= " CHUNK_INTERVAL_DEFAULT_STR ".",
		"milliseconds", QString(CHUNK_INTERVAL_DEFAULT_STR));

	// Shows potential queries in help text.
	QString query_examples = "XML query (XPath):\n"
							 "  Example 1: \"type='EEG'\"\n"
							 "  Example 2 (clause): \"name='Tobii' and type='Eyetracker'\"\n"
							 "  Example 3 (wildcard): \"contains(name, 'Player 1 EEG')\"";

	// Call parse() to find out the command (if there is one).
	parser.parse(QCoreApplication::arguments());

	const QStringList args = parser.positionalArguments();
	const QString command = args.isEmpty() ? QString() : args.first();


	if (command == "record") {
		// Add command description.
		commandParser.setApplicationDescription("\nStart an LSL recording.\n");

		// Add timeout option.
		commandParser.addOption(timeout_option);

		// Add resolve timeout option.
		commandParser.addOption(resolve_timeout_option);

		// Add collect offsets option.
		commandParser.addOption(collect_offsets_option);
		
		// Add enable recording timestamps option.
		commandParser.addOption(recording_timestamps_option);

		// Add chunk interval option.
		commandParser.addOption(chunk_interval_option);

		// Add post processing option.
		commandParser.addOption(post_processing_option);

		// Describe recording command (for usage portion of help text).
		commandParser.addPositionalArgument(EMPTY_PLACEHOLDER, EMPTY_PLACEHOLDER, "record");

		// Query option.
		commandParser.addPositionalArgument("query", query_examples);

		// Filename option.
		commandParser.addPositionalArgument("filename",
			"Filename (or basename for CSV):\n"
			"  Example 1: \"recording.xdf\"\n"
			"  Example 2 (CSV base name): \"recording.csv\" - outputs "
			"recording<stream_name_here>.csv for each stream.");

		// Dummy arg added last to show [record_options] after other positional args (basically for
		// help only).
		commandParser.addPositionalArgument(
			EMPTY_PLACEHOLDER, EMPTY_PLACEHOLDER, "[record_options]");

		QStringList positional_args;
		process_command(commandParser, app, positional_args, 2);
		QString query = positional_args[1];
		QString filename = positional_args[2];
		QString timeout_str = commandParser.value(timeout_option);
		QString resolve_timeout_str = commandParser.value(resolve_timeout_option);
		QString post_processing_str = commandParser.value(post_processing_option);
		QString chunk_interval_str = commandParser.value(chunk_interval_option);

		double timeout = parse_timeout(timeout_str, timeout_option.names());
		double resolve_timeout = parse_resolve_timeout(resolve_timeout_str, resolve_timeout_option.names());
		int post_processing_flag = parse_post_processing(post_processing_str, post_processing_option.names());
		std::chrono::milliseconds chunk_interval = parse_chunk_interval(chunk_interval_str, chunk_interval_option.names());

		bool collect_offsets = commandParser.isSet(collect_offsets_option);
		bool recording_timestamps = commandParser.isSet(recording_timestamps_option);

		// Simple validation of filename (must be csv or xdf(z) file).
		file_type_t filetype;
 			if (filename.endsWith(".csv")) {
			filetype = file_type_t::csv;
		} else if (filename.endsWith(".xdf")) {
			filetype = file_type_t::xdf;
		} else {
			std::stringstream msg;
			msg << "Badly formed filename received: " << filename.toStdString()
				<< " filename must end in .xdf, .xdfz or .csv.";
			incorrect_usage(commandParser, msg.str());
		}
		return execute_record_command(query, filename, filetype, timeout, resolve_timeout,
			collect_offsets, recording_timestamps, post_processing_flag, chunk_interval);
	} else if (command == "list") {
		// Add command description.
		commandParser.setApplicationDescription("\nList all LSL streams.\n");

		// Add timeout option.
		commandParser.addOption(timeout_option);

		// Add resolve timeout option.
		commandParser.addOption(resolve_timeout_option);

		// Add verbose option.
		commandParser.addOption(verbose_option);

		// Describe list command (for usage portion of help text).
		commandParser.addPositionalArgument(EMPTY_PLACEHOLDER, EMPTY_PLACEHOLDER, "list");

		// Dummy arg added last to show [list_options] after other positional args (basically help
		// only).
		commandParser.addPositionalArgument(EMPTY_PLACEHOLDER, EMPTY_PLACEHOLDER, "[list_options]");

		QStringList positional_args;
		process_command(commandParser, app, positional_args, 0);
		QString timeout_str = commandParser.value(timeout_option);
		QString resolve_timeout_str = commandParser.value(resolve_timeout_option);
		double timeout = parse_timeout(timeout_str, timeout_option.names());
		double resolve_timeout = parse_resolve_timeout(resolve_timeout_str, resolve_timeout_option.names());
		bool verbose = commandParser.isSet(verbose_option);
		return execute_list_command(timeout, resolve_timeout, verbose);
	} else if (command == "find") {
		// Add command description.
		commandParser.setApplicationDescription("\nFind LSL streams via query.\n");

		// Add timeout option.
		commandParser.addOption(timeout_option);

		// Add resolve timeout option.
		commandParser.addOption(resolve_timeout_option);

		// Add verbose option.
		commandParser.addOption(verbose_option);

		// Describe find command (for usage portion of help text).
		commandParser.addPositionalArgument(EMPTY_PLACEHOLDER, EMPTY_PLACEHOLDER, "find");

		// Query option.
		commandParser.addPositionalArgument("query", query_examples);

		// Dummy arg added last to show [find_options] after other positional args (basically for
		// help only).
		commandParser.addPositionalArgument(EMPTY_PLACEHOLDER, EMPTY_PLACEHOLDER, "[find_options]");

		QStringList positional_args;
		process_command(commandParser, app, positional_args, 1);
		QString query = positional_args[1];
		QString timeout_str = commandParser.value(timeout_option);
		QString resolve_timeout_str = commandParser.value(resolve_timeout_option);
		double timeout = parse_timeout(timeout_str, timeout_option.names());
		double resolve_timeout = parse_resolve_timeout(resolve_timeout_str, resolve_timeout_option.names());
		bool verbose = commandParser.isSet(verbose_option);
		return execute_find_command(query, timeout, resolve_timeout, verbose);
	} else {
		parser.process(app); // Handles help and version args.
		incorrect_usage(parser,
			command.isEmpty()
				? "Incorrect usage, no command or arguments provided"
				: ("Incorrect usage, unknown command \"" + command.toStdString() + "\" provided"),
			true);
	}
#pragma endregion
}
