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
		if (info.matches_query(query.toStdString().c_str())) {
			streams.emplace_back(info);
			match = true;
		}
	}
	return match;
}

int start_recording(QString query, int timeout) {
	std::vector<lsl::stream_info> recordstreams;
	if (find_streams(query, timeout, recordstreams)) {
		std::cout << "Query matched " << recordstreams.size() << " streams:\n";
		for (auto &stream : recordstreams) {
			std::cout << "\n 1. " << stream.name() << " @ " << stream.hostname();
		}
	} else {
		std::cout << "Query did not match any streams.";
		return 2;
	}

	std::vector<std::string> watchfor;
	std::map<std::string, int> sync_options;
	std::cout << "Starting the recording, press Enter to quit..." << std::endl;
	recording r(query.toStdString(), recordstreams, watchfor, sync_options, true);
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

void process_command(QCommandLineParser &parser, QCoreApplication &app, QStringList &pos_args,
	int expected_num_pos_args = 0) {
	// Process args.
	parser.process(app);
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
	QCommandLineOption timeoutOption(QStringList() << "t"
												   << "timeout",
		"How long (in seconds) to wait while resolving stream(s).");

	// Call parse() to find out the command (if there is one).
	parser.parse(QCoreApplication::arguments());

	const QStringList args = parser.positionalArguments();
	const QString command = args.isEmpty() ? QString() : args.first();


	if (command == "record") {
		parser.clearPositionalArguments();

		// Add timeout option.
		parser.addOption(timeoutOption);

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

		QStringList positionalArgs;
		process_command(parser, app, positionalArgs, 2);
		QString query = positionalArgs[1];
		QString file = positionalArgs[2];
		int timeout = std::stoi(parser.value(timeoutOption).toStdString());
		return start_recording(query, timeout);
	} else {
		parser.process(app);
		if (parser.isSet("help")) { parser.showHelp(); }
		incorrect_usage(parser,
			command.isEmpty() ? "Incorrect usage, no command provided"
							  : "Incorrect usage, unknown command provided",
			true);
	}
#pragma endregion

	std::cin.get();

	return 0;
}
