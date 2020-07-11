#include <iostream>
#include <emscripten/fetch.h>
#include <emscripten/bind.h>
#include <emscripten.h>

#include "IndexedDBFileSystem.h"

namespace IndexedFS {
	using namespace std;

	static std::string cacheFolder = "ems_test_cache";
	static unsigned int request_id = 0;

	static void OnLoadComplete(void* args, void* buffer, int size) {
		ResultCallbackHandler* rc_handler = (ResultCallbackHandler*)args;

		if (rc_handler == nullptr) {
			cout << "OnLoadComplete error(ResultCallbackHandler is null)" << endl;
			return;
		}

		cout << "OnLoadComplete success(" << rc_handler->GetRequestId() << "): filename(" << rc_handler->GetFileName() << "):  result (" << size << ")" << endl;
		const char* res_data = (const char*)buffer;

		if (rc_handler->loadHandler) {
			rc_handler->loadHandler((bool)true, (const char*)res_data, (int)size);
		}
	
		//std::string result = std::string((char*)buffer, size);

		//cout << "OnLoadComplete finished(" << rc_handler->GetRequestId() << "): result string (" << result << ")" << endl;

		delete rc_handler;
	}

	static void OnLoadFailed(void* args) {
		ResultCallbackHandler* rc_handler = (ResultCallbackHandler*)args;

		if (rc_handler == nullptr) {
			cout << "OnLoadFailed error(ResultCallbackHandler is null)" << endl;
			return;
		}

		cout << "OnLoadFailed success(" << rc_handler->GetRequestId() << "): filename(" << rc_handler->GetFileName() << ")" << endl;
		
		if (rc_handler->loadHandler) {
			rc_handler->loadHandler((bool)false, nullptr, 0);
		}

		cout << "OnLoadFailed finished(" << rc_handler->GetRequestId() << ")" << endl;

		delete rc_handler;
	}

	static void OnStoreComplete(void* args) {
		ResultCallbackHandler* rc_handler = (ResultCallbackHandler*)args;

		if (rc_handler == nullptr) {
			cout << "OnStoreComplete error(ResultCallbackHandler is null)" << endl;
			return;
		}

		cout << "OnStoreComplete(" << rc_handler->GetRequestId() << "): filename(" << rc_handler->GetFileName() << ")" << endl;

		if (rc_handler->saveHandler) {
			rc_handler->saveHandler((bool)true);
		}
		cout << "OnStoreComplete finished(" << rc_handler->GetRequestId() << ")" << endl;

		delete rc_handler;
	}

	static void OnStoreFailed(void* args) {
		ResultCallbackHandler* rc_handler = (ResultCallbackHandler*)args;

		if (rc_handler == nullptr) {
			cout << "OnStoreFailed error(ResultCallbackHandler is null)" << endl;
			return;
		}

		cout << "OnStoreFailed(" << rc_handler->GetRequestId() << "): filename(" << rc_handler->GetFileName() << ")" << endl;

		if (rc_handler->saveHandler) {
			rc_handler->saveHandler((bool)false);
		}
		cout << "OnStoreFailed finished(" << rc_handler->GetRequestId() << ")" << endl;

		delete rc_handler;
	}

	static void OnCheckSuccess(void* args, int result) {
		ResultCallbackHandler* rc_handler = (ResultCallbackHandler*)args;

		if (rc_handler == nullptr) {
			cout << "OnCheckSuccess -> Error! ResultCallbackHandler is null" << endl;
			return;
		}

		cout << "OnCheckSuccess(" << rc_handler->GetRequestId() << ") : filename(" << rc_handler->GetFileName() << "): result (" << result << ")" << endl;

		if (rc_handler->checkHandler) {
			rc_handler->checkHandler(result == 1, false);
		}
		cout << "OnCheckSuccess finished(" << rc_handler->GetRequestId() << ")" << endl;

		delete rc_handler;
	}

	static void OnCheckFailed(void* args) {
		ResultCallbackHandler* rc_handler = (ResultCallbackHandler*)args;

		if (rc_handler == nullptr) {
			cout << "OnCheckFailed -> Error! ResultCallbackHandler is null" << endl;
			return;
		}

		cout << "OnCheckFailed(" << rc_handler->GetRequestId() << ") : filename(" << rc_handler->GetFileName() << ")" << endl;

		if (rc_handler->checkHandler) {
			rc_handler->checkHandler(false, true);
		}
		cout << "OnCheckFailed finished(" << rc_handler->GetRequestId() << ")" << endl;

		delete rc_handler;
	}

	void IsFileInLocalStorageAsync(const std::string& db_name, const std::string& file_name, const FileCheckHandler& on_checked)
	{
		ResultCallbackHandler* handler = new ResultCallbackHandler(file_name.c_str(), request_id++);
		handler->checkHandler = on_checked;

		cout << "IsFileInLocalStorageAsync(" << handler->GetRequestId() << "): filename(" << handler->GetFileName() << ")" << endl;

		emscripten_idb_async_exists(db_name.c_str(), file_name.c_str(), (void*)handler, OnCheckSuccess, OnCheckFailed);
	}

	bool IsFileInLocalStorageSync(const std::string& db_name, const std::string& file_name)
	{
		//TODO: NOT IMPLEMENTED PROPERLY
		int id = request_id++;

		cout << "IsFileInLocalStorageSync(" << id << "): start " << endl;
		int error_code = 0;
		int is_persist = 0;
		//https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_idb_store
		emscripten_idb_exists(db_name.c_str(), file_name.c_str(), &is_persist, &error_code);
		cout << "IsFileInLocalStorageSync(" << id << "): result(" << is_persist << ")" << endl;

		return is_persist > 0;
	}

	void LoadFileFromLocalStorageAsync(const std::string& db_name, const std::string& file_name, const FileLoadHandler& on_load)
	{
		ResultCallbackHandler* handler = new ResultCallbackHandler(file_name.c_str(), request_id++);
		handler->loadHandler = on_load;

		cout << "LoadFileFromLocalStorage(" << handler->GetRequestId() << "): filename(" << handler->GetFileName() << ")" << endl;
		
		//More info here https://emscripten.org/docs/api_reference/emscripten.h.html#asynchronous-indexeddb-api -> emscripten_idb_async_store
		emscripten_idb_async_load(db_name.c_str(), file_name.c_str(), (void*)handler, OnLoadComplete, OnLoadFailed);
	}

	bool LoadFileFromLocalStorageSync(const std::string& db_name, const std::string& file_name, const char* bytes_result, int& bytes_count)
	{
		//TODO: NOT IMPLEMENTED PROPERLY
		unsigned int id = request_id++;

		void* buffer = ((void*)bytes_result);
		int error = 0;
		cout << "LoadFileFromLocalStorageSync(" << id << "): start" << endl;
		//More info here https://emscripten.org/docs/api_reference/emscripten.h.html#indexeddb -> emscripten_idb_load
		emscripten_idb_load(db_name.c_str(), file_name.c_str(), &buffer, &bytes_count, &error);

		cout << "LoadFileFromLocalStorageSync(" << id << "): url(" << file_name << ") - size(" << bytes_count << ") - error(" << error << ")" << endl;

		return error == 0;
	}

	bool SaveFileToIndexDBSync(const std::string& db_name, const std::string& file_name, const char* file_data, size_t size)
	{
		unsigned int id = request_id++;
		//TODO: SYNC NOT IMPLEMENTED PROPERLY
		int error_code = 0;

		cout << "SaveFileToIndexDBSync(" << id << "): start" << endl;

		//More info here https://emscripten.org/docs/api_reference/emscripten.h.html#indexeddb -> emscripten_idb_store
		emscripten_idb_store(db_name.c_str(), file_name.c_str(), (void*)file_data, size, &error_code);
		cout << "SaveFileToIndexDBSync(" << id << "): filename("<< file_name << "), code(" << error_code << ")" << endl;

		return error_code == 0;
	}

	void SaveFileToIndexDBAsync(const std::string& db_name, const std::string& file_name, const char* file_data, size_t size, FileSaveHandler& on_save)
	{		
		ResultCallbackHandler* handler = new ResultCallbackHandler(file_name.c_str(), request_id++);
		handler->saveHandler = on_save;

		cout << "SaveFileToIndexDBAsync(" << handler->GetRequestId() << ") started, filename(" << handler->GetFileName() << ")" << endl;
				
		//More info here https://emscripten.org/docs/api_reference/emscripten.h.html#indexeddb -> emscripten_idb_store
		emscripten_idb_async_store(db_name.c_str(), file_name.c_str(), (void*)file_data, size, (void*)handler, OnStoreComplete, OnStoreFailed);
	}

	/*
		void IsFileInLocalStorage(const std::string& file_name)
		{
			long id = request_id++;
			cout << "IsFileInLocalStorage URL (" << id << "): " << file_name << endl;

			emscripten_idb_async_exists(cacheFolder.c_str(), file_name.c_str(), (void*)file_name.c_str(), OnCheckSuccess, OnCheckFailed);
		}*/


		//void SaveFileToIndexDB(const char* file_name, const char* file_data, size_t size)
		//{
		//	unsigned int id = request_id++;
		//	cout << "SaveFileToIndexDB(" << id << "): " << file_name << endl;
		//	//https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_idb_async_store
		//	emscripten_idb_async_store(cacheFolder.c_str(), file_name, (void*)file_data, size, &id, OnStoreComplete, OnStoreFailed);

		//	std::string file_name_sync = "sync_" + std::string(file_name);

		//	int error_code = -1;
		//	//https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_idb_store
		//	emscripten_idb_store(cacheFolder.c_str(), file_name, (void*)file_data, size, &error_code);
		//	cout << "SaveFileToIndexDB_sync(" << id << "): " << error_code << endl;
		//}



	//bool LoadFileFromLocalStorage(const std::string& file_name)
	//{
	//	int id = request_id++;
	//	int* pointer = &id;

	//	cout << "LoadFileFromLocalStorage(" << id << "): " << file_name << endl;
	//	emscripten_idb_async_load(cacheFolder.c_str(), file_name.c_str(), &id, OnLoadComplete, OnLoadFailed);
	//	return true;
	//}

}