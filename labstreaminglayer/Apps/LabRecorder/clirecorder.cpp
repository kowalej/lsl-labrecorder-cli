#include "process.h"
#include "recording.h"
#include "xdfwriter.h"
#include <QCommandLineParser>
#include <Windows.h>


std::string remove_chars(const std::string &source, const std::string &chars) {
	std::string result = "";
	for (unsigned int i = 0; i < source.length(); i++) {
		bool foundany = false;
		for (unsigned int j = 0; j < chars.length() && !foundany; j++) {
			foundany = (source[i] == chars[j]);
		}
		if (!foundany) { result += source[i]; }
	}
	return result;
}

bool replace(std::string &str, const std::string &from, const std::string &to) {
	size_t start_pos = str.find(from);
	if (start_pos == std::string::npos) return false;
	str.replace(start_pos, from.length(), to);
	return true;
}

bool find_streams(QString query, double timeout, std::vector<lsl::stream_info> &streams) {
	query =
		query.replace("\"", "'"); // Double quotes should be single quotes for LSL query to work.

	bool match = false;
	for (const auto &info : lsl::resolve_streams(timeout)) {
		if (query == "*" || info.matches_query(query.toStdString().c_str())) {
			streams.emplace_back(info);
			match = true;
		}
	}
	return match;
}

void display_stream_info(std::vector<lsl::stream_info> &streams, bool matches, QString query) {
	bool query_mode = !query.isEmpty();

	if (matches) {
		std::string preamble = query_mode ? "Query matched " : "Found "; 
		std::cout << preamble << streams.size() << " stream" << (streams.size() > 1 ? "s:" : ":") << std::endl;
		int index = 1;
		for (auto &stream : streams) {
			std::cout << index << ". " << stream.name() << " @ " << stream.hostname() << std::endl;
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

int execute_list_command(double timeout) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams("*", timeout, streams);
	display_stream_info(streams, matches, "");

	return matches ? 0 : 2;
}

int execute_find_command(QString query, double timeout) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams(query, timeout, streams);
	display_stream_info(streams, matches, query);

	return matches ? 0 : 2;
}

int execute_record_command(QString query, QString filename, double timeout) {
	std::vector<lsl::stream_info> streams;
	bool matches = find_streams(query, timeout, streams);
	display_stream_info(streams, matches, query);

	// End command if no matches found.
	if (!matches) { return 2; }

	std::vector<std::string> watchfor;
	std::map<std::string, int> sync_options;
	std::cout << "--- Starting the recording, press ENTER to quit... ---" << std::endl;
	recording r(filename.toStdString(), streams, watchfor, sync_options, true);
	std::cin.get();
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
	return timeout_str.isEmpty() ? 1 : std::stod(timeout_str.toStdString());
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

	parser.addPositionalArgument("command", "The command to execute. See commands below:\n"
											"record - Start an LSL recording.\n"
											"list - List all LSL streams.\n"
											"find - Apply query to find LSL stream(s).\n");

	// Timeout option (-t, --timeout).
	QCommandLineOption timeout_option(QStringList() << "t"
												   << "timeout",
		"How long (in seconds) to wait while resolving stream(s).");

	// Call parse() to find out the command (if there is one).
	parser.parse(QCoreApplication::arguments());

	const QStringList args = parser.positionalArguments();
	const QString command = args.isEmpty() ? QString() : args.first();


	if (command == "record") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeout_option);

		// Describe recording command (basically for help only).
		parser.addPositionalArgument(
			"record", "Start an LSL recording.", "record [record_options]");

		// Query option.
		parser.addPositionalArgument("query",
			"XML stream query: \n"
			"    Example 1: \"type='EEG'\" \n"
			"    Example 2: \"name='Tobii' and type='Eyetracker'\" \n");

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
		double timeout = parse_timeout(timeout_str);
		return execute_record_command(query, filename, timeout);
	} 
	else if (command == "list") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeout_option);

		// Describe find command (basically for help only).
		parser.addPositionalArgument("list", "List all LSL streams.", "list [list_options]");

		QStringList positional_args;
		process_command(parser, app, positional_args, 0);
		QString timeout_str = parser.value(timeout_option);
		double timeout = parse_timeout(timeout_str);
		return execute_list_command(timeout);
	}
	else if (command == "find") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeout_option);

		// Describe find command (basically for help only).
		parser.addPositionalArgument(
			"find", "Find LSL stream(s).", "find [find_options]");

		// Query option.
		parser.addPositionalArgument("query",
			"XML stream query: \n"
			"    Example 1: \"type='EEG'\" \n"
			"    Example 2: \"name='Tobii' and type='Eyetracker'\" \n");

		QStringList positional_args;
		process_command(parser, app, positional_args, 1);
		QString query = positional_args[1];
		QString timeout_str = parser.value(timeout_option);
		double timeout = parse_timeout(timeout_str);
		return execute_find_command(query, timeout);
	}
	else {
		parser.process(app);
		if (parser.isSet("help")) { parser.showHelp(); }
		incorrect_usage(parser,
			command.isEmpty() ? "Incorrect usage, no command provided"
							  : "Incorrect usage, unknown command provided",
			true);
	}
#pragma endregion
}
