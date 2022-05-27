#define CATCH_CONFIG_MAIN

#include <catch2/catch.hpp>

#include <NifFile.hpp>

using namespace nifly;

bool CompareBinaryFiles(const std::filesystem::path& fileName1, const std::filesystem::path& fileName2) {
	// Open file streams at the end of each file
	std::ifstream fileStream1(fileName1, std::ifstream::ate | std::ifstream::binary);
	std::ifstream fileStream2(fileName2, std::ifstream::ate | std::ifstream::binary);

	// Compare file size to start with
	if (fileStream1.tellg() != fileStream2.tellg())
		return false;

	// Rewind to the beginning of each stream
	fileStream1.seekg(0);
	fileStream2.seekg(0);

	std::istreambuf_iterator<char> fileBegin1(fileStream1);
	std::istreambuf_iterator<char> fileBegin2(fileStream2);

	// Compare binary contents
	return std::equal(fileBegin1, std::istreambuf_iterator<char>(), fileBegin2);
}

const std::string nifSuffix = ".nif";
const std::string nifSuffixOutput = "_output" + nifSuffix;
const std::string nifSuffixExpected = "_expected" + nifSuffix;

std::tuple<std::string, std::string, std::string> GetNifFileTuple(const char* fileName) {
	std::string fileInput = fileName + nifSuffix;
	std::string fileOutput = fileName + nifSuffixOutput;
	std::string fileExpected = fileName + nifSuffixExpected;
	return std::make_tuple(fileInput, fileOutput, fileExpected);
}

TEST_CASE("Load not existing file", "[NifFile]") {
	constexpr auto fileName = "not_existing.nif";

	NifFile nif;
	REQUIRE(nif.Load(fileName) != 0);
}

TEST_CASE("Load and save static file (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Static_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save static file (FO4)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Static_FO4";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save static file (FO4, Version 132)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Static_FO4_132";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save static file (FO4, Version 139)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Static_FO4_139";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save skinned file (OB)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Skinned_OB";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save skinned file (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Skinned_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save skinned, dynamic file (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Skinned_Dynamic_SE";
	const auto[fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save skinned file (FO4)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Skinned_FO4";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save furniture file (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Furniture_Col_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save file with loose blocks (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_LooseBlocks_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save file with multi bound node (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_MultiBound_SE";
	const auto[fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save animated file (LE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Animated_LE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save file with deep scene graph (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_DeepGraph_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load, optimize (LE to SE) and save file", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Optimize_LE_to_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	OptOptions options;
	options.targetVersion = NiVersion::getSSE();

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	nif.OptimizeFor(options);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load, optimize (LE to SE, dynamic) and save file", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Optimize_Dynamic_LE_to_SE";
	const auto[fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	OptOptions options;
	options.targetVersion = NiVersion::getSSE();
	options.headParts = true;

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	nif.OptimizeFor(options);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load, optimize (SE to LE) and save file", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Optimize_SE_to_LE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	OptOptions options;
	options.targetVersion = NiVersion::getSK();

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	nif.OptimizeFor(options);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save file with ordered node (SE)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_OrderedNode_SE";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	OptOptions options;
	options.targetVersion = NiVersion::getSK();

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load, optimize (SE to LE, dynamic) and save file", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_Optimize_Dynamic_SE_to_LE";
	const auto[fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	OptOptions options;
	options.targetVersion = NiVersion::getSK();
	options.headParts = true;

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	nif.OptimizeFor(options);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save file with non-zero index root node", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_RootNonZero";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}

TEST_CASE("Load and save file (FO76)", "[NifFile]") {
	constexpr auto fileName = "TestNifFile_FO76";
	const auto [fileInput, fileOutput, fileExpected] = GetNifFileTuple(fileName);

	NifFile nif;
	REQUIRE(nif.Load(fileInput) == 0);
	REQUIRE(nif.Save(fileOutput) == 0);

	REQUIRE(CompareBinaryFiles(fileOutput, fileExpected));
}
