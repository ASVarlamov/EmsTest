#pragma once
#include <iostream>
#include <functional>

namespace IndexedFS {
	using namespace std;

	typedef std::function<void(bool result, bool is_error)> FileCheckHandler;
	typedef std::function<void(bool result, const char* bytes, int size)> FileLoadHandler;
	typedef std::function<void(bool result)> FileSaveHandler;

	class ResultCallbackHandler {
	public:
		ResultCallbackHandler(const char* file_name, unsigned int request_id) {
			_requestId = request_id;
			_filename = std::move(std::string(file_name));
		};

		FileCheckHandler checkHandler;
		FileLoadHandler loadHandler;
		FileSaveHandler saveHandler;

		const int GetRequestId() const {
			return _requestId;
		}		
		
		const std::string GetFileName() const {
			return _filename;
		}

	private:
		int _requestId;
		std::string _filename;
	};

	void IsFileInLocalStorageAsync(const std::string& db_name, const std::string& file_name, const FileCheckHandler& on_checked);
	bool IsFileInLocalStorageSync(const std::string& db_name, const std::string& file_name);

	void LoadFileFromLocalStorageAsync(const std::string& db_name, const std::string& file_name, const FileLoadHandler& on_load);
	bool LoadFileFromLocalStorageSync(const std::string& db_name, const std::string& file_name, const char* bytes_result, int& bytes_count);

	void SaveFileToIndexDBAsync(const std::string& db_name, const std::string& file_name, const char* file_data, size_t size, FileSaveHandler& on_save);
	bool SaveFileToIndexDBSync(const std::string& db_name, const std::string&, const char* file_data, size_t size);
}

