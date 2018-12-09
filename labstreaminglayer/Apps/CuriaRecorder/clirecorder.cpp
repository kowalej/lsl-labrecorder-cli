#include "LSLStreamWriter.h"
#include "process.h"
#include "recording.h"
#include <QCommandLineParser>
#include <Windows.h>
#include <conio.h>
#include <iostream>
#include <time.h>
#include <csignal>

#define TIMEOUT_DEFAULT 5
#define RESOLVE_TIMEOUT_DEFAULT 1


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

	std::cout << "Searching for streams..." << std::endl;

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

	if (matches) {
		std::string preamble = query_mode ? "Query matched " : "Found ";
		std::cout << preamble << streams.size() << " stream" << (streams.size() > 1 ? "s:" : ":")
				  << std::endl;
		int index = 1;
		for (auto &stream : streams) {
			std::cout << index << ". " << stream.name() << " @ " << stream.hostname() << std::endl;
			if (verbose) { std::cout << stream.as_xml() << std::endl; }
			index++;
		}
		std::cout << std::endl;
	} else {
		if (query_mode) {
			std::cout << "Query " << query.toStdString() << " did not match any streams.";
		} else {
			std::cout << "No streams were found.";
		}
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
	double resolve_timeout, bool collect_offsets, bool recording_timestamps) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams(query, timeout, resolve_timeout, streams);
	display_stream_info(streams, matches, query);

	// End command if no matches found.
	if (!matches) { return 2; }


	std::vector<std::string> watchfor;
	std::map<std::string, int> sync_options; // Not yet supported.
	std::cout << "-------------------------------------------------------" << std::endl;
	std::cout << "--- Starting the recording, press Ctrl+C to quit... ---" << std::endl;
	std::cout << "-------------------------------------------------------" << std::endl;
	recording r(
		filename.toStdString(), 
		file_type, 
		streams, 
		watchfor, 
		sync_options, 
		collect_offsets,
		recording_timestamps);
	signal(SIGINT, exitHandler); // Check for Ctrl + C hit to cancel.
	while (NOEXIT) { 
		std::this_thread::sleep_for(std::chrono::milliseconds(1000)); 
	}
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

double parse_timeout(QString timeout_str) {
	return timeout_str.isEmpty() ? TIMEOUT_DEFAULT : std::stod(timeout_str.toStdString());
}

double parse_resolve_timeout(QString resolve_timeout_str) {
	return resolve_timeout_str.isEmpty() ? RESOLVE_TIMEOUT_DEFAULT
										 : std::stod(resolve_timeout_str.toStdString());
}

void process_command(QCommandLineParser &parser, QCoreApplication &app, QStringList &pos_args,
	int expected_num_pos_args = 0) {
	// Process args.
	parser.process(app);
	parser.process(QCoreApplication::arguments());
	if (parser.isSet("help")) { parser.showHelp(); }
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
											"find - Apply query to find LSL stream(s).\n");

	// Timeout option (-t, --timeout). Default value = 5.
	QCommandLineOption timeout_option(QStringList() << "t"
													<< "timeout",
		"Maxmimum overall time (in seconds) to wait while searching for stream(s).", "timeout",
		"5");

	// Resolve timeout option (-l, --lsl-resolve-timeout). Default value = 1.
	QCommandLineOption resolve_timeout_option(QStringList() << "l"
															<< "lsl-resolve-timeout",
		"Time (in seconds) to wait during each LSL call to resolve stream(s).", "lsl-resolve-timeout",
		"1");

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

	QString query_examples = "XML stream query: \n"
							 "    Example 1: \"type='EEG'\" \n"
							 "    Example 2 (clause): \"name='Tobii' and type='Eyetracker'\" \n"
							 "    Example 3 (wildcard): \"contains(name, 'Player 1 EEG')\" \n";

	// Call parse() to find out the command (if there is one).
	parser.parse(QCoreApplication::arguments());

	const QStringList args = parser.positionalArguments();
	const QString command = args.isEmpty() ? QString() : args.first();


	if (command == "record") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeout_option);

		// Add resolve timeout option.
		parser.addOption(resolve_timeout_option);

		// Add collect offsets option.
		parser.addOption(collect_offsets_option);

		// Add enable recording timestamps option.
		parser.addOption(recording_timestamps_option);

		// Describe recording command (basically for help only).
		parser.addPositionalArgument(
			"record", "Start an LSL recording.", "record [record_options]");

		// Query option.
		parser.addPositionalArgument("query", query_examples);

		// Filename option.
		parser.addPositionalArgument("filename",
			"Filename (or basename for CSV): \n"
			"    Example 1: recording.xdf \n"
			"    Example 2 (CSV base name): recording.csv - outputs "
			"recording<stream_name_here>.csv for each stream.");

		QStringList positional_args;
		process_command(parser, app, positional_args, 2);
		QString query = positional_args[1];
		QString filename = positional_args[2];
		QString timeout_str = parser.value(timeout_option);
		QString resolve_timeout_str = parser.value(resolve_timeout_option);
		double timeout = parse_timeout(timeout_str);
		double resolve_timeout = parse_resolve_timeout(resolve_timeout_str);
		bool collect_offsets = parser.isSet(collect_offsets_option);
		bool recording_timestamps = parser.isSet(recording_timestamps_option);

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
			incorrect_usage(parser, msg.str());
		}
		return execute_record_command(query, filename, filetype, timeout, resolve_timeout,
			collect_offsets, recording_timestamps);
	} else if (command == "list") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeout_option);

		// Add resolve timeout option.
		parser.addOption(resolve_timeout_option);

		// Add verbose option.
		parser.addOption(verbose_option);

		// Describe find command (basically for help only).
		parser.addPositionalArgument("list", "List all LSL streams.", "list [list_options]");

		QStringList positional_args;
		process_command(parser, app, positional_args, 0);
		QString timeout_str = parser.value(timeout_option);
		QString resolve_timeout_str = parser.value(resolve_timeout_option);
		double timeout = parse_timeout(timeout_str);
		double resolve_timeout = parse_resolve_timeout(resolve_timeout_str);
		bool verbose = parser.isSet(verbose_option);
		return execute_list_command(timeout, resolve_timeout, verbose);
	} else if (command == "find") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeout_option);

		// Add resolve timeout option.
		parser.addOption(resolve_timeout_option);

		// Add verbose option.
		parser.addOption(verbose_option);

		// Describe find command (basically for help only).
		parser.addPositionalArgument("find", "Find LSL stream(s).", "find [find_options]");

		// Query option.
		parser.addPositionalArgument("query", query_examples);

		QStringList positional_args;
		process_command(parser, app, positional_args, 1);
		QString query = positional_args[1];
		QString timeout_str = parser.value(timeout_option);
		QString resolve_timeout_str = parser.value(resolve_timeout_option);
		double timeout = parse_timeout(timeout_str);
		double resolve_timeout = parse_resolve_timeout(resolve_timeout_str);
		bool verbose = parser.isSet(verbose_option);
		return execute_find_command(query, timeout, resolve_timeout, verbose);
	} else {
		parser.process(app);
		if (parser.isSet("help")) { parser.showHelp(); }
		incorrect_usage(parser,
			command.isEmpty()
				? "Incorrect usage, no command provided"
				: ("Incorrect usage, unknown command \"" + command.toStdString() + "\" provided"),
			true);
	}
#pragma endregion
}
