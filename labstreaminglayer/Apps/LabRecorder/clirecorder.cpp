#include "process.h"
#include "recording.h"
#include "xdfwriter.h"
#include <QCommandLineParser>
#include <Windows.h>

PROCESS_INFORMATION guiProcess;

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

int start_recording(QString query) {
	query =
		query.replace("\"", "'"); // Double quotes should be single quotes for LSL query to work.

	std::vector<lsl::stream_info> infos = lsl::resolve_streams(), recordstreams;

	bool matched = false;
	for (const auto &info : infos) {
		if (info.matches_query(query.toStdString().c_str())) {
			std::cout << "Found " << info.name() << '@' << info.hostname();
			std::cout << " matching '" << query.toStdString() << "'\n";
			matched = true;
			recordstreams.emplace_back(info);
		}
	}
	if (!matched) {
		std::cout << '"' << query.toStdString() << "\" matched no stream!\n";
		return 2;
	}

	std::vector<std::string> watchfor;
	std::map<std::string, int> sync_options;
	std::cout << "Starting the recording, press Enter to quit" << std::endl;
	recording r(query.toStdString(), recordstreams, watchfor, sync_options, true);
	return 0;
}

void start_gui() { guiProcess = Process::launch_process("LabRecorder.exe", ""); }

int main(int argc, char **argv) {
	#pragma region Basic app setup
	QCoreApplication app(argc, argv);
	QCoreApplication::setApplicationName("Curia Recorder");
	QCoreApplication::setApplicationVersion("1.0");
	#pragma endregion

	#pragma region Process options
	QCommandLineParser parser;
	parser.setApplicationDescription("\nRecord and discover LSL streams.");
	parser.addHelpOption();
	parser.addVersionOption();

	parser.addPositionalArgument("command", "The command to execute. See commands below:\n"
											"* record - Start an LSL recording.");

	// GUI option used for multiple commands.
	QCommandLineOption guiOption(QStringList() << "g"
											   << "gui",
		"Record using controllable GUI (LabRecorder.exe).");

	// Call parse() to find out the positional arguments.
	parser.parse(QCoreApplication::arguments());

	const QStringList args = parser.positionalArguments();
	const QString command = args.isEmpty() ? QString() : args.first();

	if (command == "record") {
		parser.clearPositionalArguments();

		// Record using the GUI (-g, --gui).
		parser.addOption(guiOption);

		parser.addPositionalArgument(
			"record", "Start an LSL recording.", "record [record_options]");

		// String query option.
		parser.addPositionalArgument("query",
			"XML stream query: \n"
			"Example 1: \"type='EEG'\" \n"
			"Example 2: \"name='Tobii' and type='Eyetracker'\" \n");

		// Process args.
		parser.process(app);
		QStringList positionalArgs = parser.positionalArguments();
		QString query = positionalArgs[1];
		bool guiSet = parser.isSet(guiOption);
		if (guiSet) { start_gui(); }
		// return start_recording(query);
	} else {
		parser.process(app);
		if (parser.isSet("help")) {
			parser.showHelp();
		} else {
			std::cout << "\n---- Incorrect usage, see below. ----\n" << std::endl;
			parser.showHelp(2);
		}
	}
	#pragma endregion

	std::cin.get();
	Process::stop_process(guiProcess);
	return 0;
}
