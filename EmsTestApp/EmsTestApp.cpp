// EmsTestApp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <map>
#include <emscripten/fetch.h>
#include <emscripten.h>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>

#include "IndexedDBFileSystem.h"

std::string bool_cast(const bool b) {
	return b ? "true" : "false";
}

using namespace std;

static bool cuurent_choice = false;
class EmsFetch;
static std::map<uint32_t, EmsFetch*> callbacks;

static bool localStorageInited = false;
static void LocalStorageInit()
{
	if (localStorageInited) return;

	EM_ASM(
		console.log('LocalStorageInit');
		FS.mkdir('/cache');
		FS.mount(IDBFS, {}, '/cache');
		FS.syncfs(true, function(err) {
			console.log(err);
		});
	);

	localStorageInited = true;
}

EM_JS(void, saveToIndexDBJS, (const char* filename, const char* bytes, int bytes_count ),
{
		console.log(UTF8ToString(filename));
		console.log('Bytes - ' + bytes);
		console.log('Size - ' + bytes_count);
		var stream = FS.open('/cache/' + UTF8ToString(filename), 'w+');
		var data = new Uint8Array(bytes_count);
		FS.write(stream, data, 0, bytes_count, 0, /*canOwn=*/false);
		FS.close(stream);

		FS.syncfs(function(err) {

			//FS.writeFile(UTF8ToString(filename), new ArrayBuffer(bytes, bytes_count));
		});
	

});

enum HttpError {
	None,
	Aborted,
	AlreadyStarted,
	BadRequest,
	Timeout
};
enum HttpRequest {
	GET,
	POST,
	HEAD,
};

HttpError MakeErrorCode() {
	return None;
}

typedef std::map<std::string, std::string> HttpHeaders;

class Request {
public:
	Request() {}
	std::string method;
	std::string url;
	std::string data;
	std::map<std::string, std::string> headers;
};

class Response {
public:
	Response() {}

	const uint8_t* resp_data;
	uint64_t resp_data_length;
	HttpHeaders resp_data_headers;
	int responseCode;

	std::string GetContentString() {
		return std::string((const char*)resp_data, resp_data_length);
	}
};

typedef void(*HttpRequestHandler)(HttpError ec, Response& realResponse);
typedef void(*HttpHeadersHandler)(HttpError ec, std::map<std::string, std::string> headers);

const std::string db_name = "ems_test_cache";

class EmsFetch
{
public:
	HttpHeadersHandler onHeadersReceived;
	HttpRequestHandler onRequestComplete;

	emscripten_fetch_t* currentFetch = nullptr;
	const char* currentRequestData = nullptr;

	void OnReadHeaders(const HttpHeadersHandler& handler, HttpError ec) {
		if (IsRunning()) {
			ec = HttpError::AlreadyStarted;
			return;
		}

		ec = HttpError::None;

		onHeadersReceived = handler;
	}


	void QueryAsync(const Request& request, const HttpRequestHandler &handler, bool from_cache = true) {
		onRequestComplete = handler;


		char* content = (char*)"";

		std::cout << "Start async(" << request.url << ")" << endl;

		if (from_cache) {
			IndexedFS::FileLoadHandler load_handler = [this, request, handler](bool rs, const char* bytes, int size) {
				std::cout << "Handler load result: (" << bool_cast(rs) << "): (" << size << ")" << endl;

				if (rs) {
					Response resp;
					resp.resp_data = (const uint8_t*)bytes;
					resp.resp_data_length = size;

					handler(HttpError(), resp);
				}
				else {
					QueryAsync(request, handler, false);
				}
			};

			IndexedFS::FileCheckHandler hand = [this, request, handler, load_handler](bool res, bool is_error) {
				std::cout << "Sync result: (" << bool_cast(res) << ")" << endl;

				if (res) {
					IndexedFS::LoadFileFromLocalStorageAsync(db_name, request.url.c_str(), load_handler);
				}
				else {
					QueryAsync(request, handler, false);
				}
			};
			IndexedFS::IsFileInLocalStorageAsync(db_name, request.url.c_str(), hand);
		}
		else
		{
			std::cout << "Sync result: (not found)" << endl;

			// определение параметров заголовка запроса
			const char *method = nullptr;

			method = request.method.c_str();
			if (request.method == "POST") {
				strcpy(content, request.data.c_str());
			}

			emscripten_fetch_attr_t attr;
			emscripten_fetch_attr_init(&attr);

			strcpy(attr.requestMethod, method);
			attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_APPEND;

			//currentRequestData = content;;

			//attr.requestData = "{'sample_field1': 'sample_data1','sample_field2' : 'sample_data2'}";
			attr.requestData = content;
			attr.requestDataSize = strlen(content);

			std::string req_data = std::string(attr.requestData, attr.requestDataSize);

			cout << "Set request data(size: " << attr.requestDataSize << "): " << req_data << endl;

			int size = request.headers.size() * 2 + 1;
			char** custom_headers = new char*[size];

			int count = 0;
			for (const auto &header : request.headers) {
				custom_headers[count++] = (char*)header.first.c_str();
				custom_headers[count++] = (char*)header.second.c_str();
				cout << "Add header: " << header.first << ": " << header.second << endl;
			}

			custom_headers[count] = 0;

			attr.requestHeaders = custom_headers;

			attr.onsuccess = OnSuccess;
			attr.onerror = OnFail;
			attr.onprogress = OnProgress;
			attr.onreadystatechange = OnHeaders;

			// or flag EMSCRIPTEN_FETCH_SYNCHRONOUS
			currentFetch = emscripten_fetch(&attr, request.url.c_str());

			callbacks[currentFetch->id] = this;

			cout << "Start fetch(" << currentFetch->id << "): " << std::string(currentFetch->url) << endl;
		}	
	}

	void Cancel() {
		if (IsRunning()) {
			cout << "Cancel fetch(" << currentFetch->id << "): " << std::string(currentFetch->url) << endl;
			callbacks[currentFetch->id] = nullptr;
			//TODO: send Abort to callback
			emscripten_fetch_close(currentFetch);
		}

		currentFetch = nullptr;
		currentRequestData = nullptr;
	}

	bool IsRunning() {
		if (currentFetch != nullptr && IsRequestActive(currentFetch->id)) {
			return true;
		}

		return false;
	}

	long GetBytesTransferred() {
		if (!IsRunning()) {
			return 0;
		}

		return currentFetch->numBytes;
	}

	long GetContentLength() {
		if (!IsRunning()) {
			return 0;
		}

		return currentFetch->numBytes;
	}


	static bool IsRequestActive(uint32_t request_id) {
		return callbacks.find(request_id) != callbacks.end();
	}

	static bool RemoveRequestInfo(uint32_t request_id) {
		cout << "RemoveRequestInfo. start(" << request_id << ")" << endl;
		if (IsRequestActive(request_id)) {
			auto req = callbacks.find(request_id);
			cout << "RemoveRequestInfo. found(" << request_id << "): " << req->first << endl;
			callbacks.erase(req);
			cout << "RemoveRequestInfo. complete(" << request_id << ") " << endl;
		}

		return false;
	}

	static void OnProgress(emscripten_fetch_t *fetch) {	}

	static HttpHeaders GetHeaders(emscripten_fetch_t *fetch) {
		if (fetch->readyState != 2) {
			return HttpHeaders();
		}

		HttpHeaders headers = HttpHeaders();

		size_t headersLengthBytes = emscripten_fetch_get_response_headers_length(fetch) + 1;
		char *headerString = new char[headersLengthBytes];

		emscripten_fetch_get_response_headers(fetch, headerString, headersLengthBytes);
		cout << "GetHeaders. (" << fetch->id << "): " << " , url = " << std::string(fetch->url) << " , response: " << std::string(headerString) << endl;

		std::string headers_string = std::string(headerString);

		char **responseHeaders = emscripten_fetch_unpack_response_headers(headerString);

		delete[] headerString;
		int numHeaders = 0;
		for (; responseHeaders[numHeaders * 2]; ++numHeaders) {
			headers.emplace(std::string(responseHeaders[numHeaders * 2]), std::string(responseHeaders[(numHeaders * 2) + 1]));
		}

		emscripten_fetch_free_unpacked_response_headers(responseHeaders);

		cout << "GetHeaders. Headers total(" << fetch->id << "): " << numHeaders << endl;

		return headers;
	}

	static void OnHeaders(emscripten_fetch_t *fetch) {
		if (fetch->readyState != 2) {
			return;
		}

		if (IsRequestActive(fetch->id)) {
			EmsFetch* client = (EmsFetch*)callbacks[fetch->id];
			if (client->onHeadersReceived != nullptr) {
				HttpHeaders headers = GetHeaders(fetch);

				cout << "OnHeaders. Headers(" << fetch->id << "): " << headers.size() << ", last-mod: " << " , url = " << std::string(fetch->url) << endl;

				client->onHeadersReceived(HttpError::None, headers);
			}
		}
	}

	static void OnSuccess(emscripten_fetch_t *fetch) {
		uint32_t fetch_id = fetch->id;
		cout << "OnSuccess. Finished downloading(" << fetch_id << "): " << fetch->numBytes << " bytes from URL: " << std::string(fetch->url) << endl;

		if (IsRequestActive(fetch_id)) {
			EmsFetch* client = (EmsFetch*)callbacks[fetch_id];

			if (client->onRequestComplete != nullptr) {
				Response response = Response();
				HttpError ec = MakeErrorCode();

				if (fetch->status > 300) {
					ec = HttpError::BadRequest;
				}
				else {
					ec = MakeErrorCode();
				}

				response.responseCode = (fetch->status);
				response.resp_data = (const uint8_t*)fetch->data;
				response.resp_data_length = fetch->numBytes;
				//cout << "OnSuccess. Data fetch(" << fetch_id << "): " << response.GetContentString() << " - " << fetch->status << " - " << std::string(fetch->url) << endl;
				cout << "OnSuccess. Data fetch(" << fetch_id << "): " << " - " << fetch->status << " - " << std::string(fetch->url) << endl;
				std::string filename = fetch->url;

				//saveToIndexDBJS(fetch->url, fetch->data, fetch->numBytes);

				response.resp_data_headers = (GetHeaders(fetch));

				IndexedFS::FileSaveHandler save_handler = [filename, fetch_id](bool result) {
					cout << "OnSaveSuccess(" << fetch_id << "), filename(" << filename << ") result(" << result << ")" << endl;
				};

				IndexedFS::SaveFileToIndexDBAsync(db_name, filename, (const char*)response.resp_data, response.resp_data_length, save_handler);
				//IndexedFS::LoadFileFromLocalStorageSync(fetch->url);

				callbacks.erase(fetch_id);
				cout << "OnSuccess. Remove request info(" << fetch_id << ")" << endl;
				client->currentFetch = nullptr;
				client->currentRequestData = nullptr;
				cout << "OnSuccess. Close(" << fetch_id << ")" << endl;
				client->onRequestComplete(ec, response);
				cout << "OnSuccess. OnComplete(" << fetch_id << ")" << endl;

				emscripten_fetch_close(fetch); // Free data associated with the fetch.
			}
		}
	}

	static void OnFail(emscripten_fetch_t *fetch) {
		uint32_t fetch_id = fetch->id;
		cout << "OnFail. Fetch(" << fetch_id << "): " << std::string(fetch->url) << " failed, HTTP failure status code: " << fetch->status << endl;

		if (IsRequestActive(fetch_id)) {
			EmsFetch* client = (EmsFetch*)callbacks[fetch_id];

			if (client->onRequestComplete != nullptr) {
				HttpError ec = HttpError::BadRequest;
				Response response = Response();

				// The data is now available at fetch->data[0] through fetch->data[fetch->numBytes-1];
				response.responseCode = (fetch->status);
				response.resp_data = (const uint8_t*)fetch->data;
				response.resp_data_length = fetch->numBytes;

				//cout << "OnFail. Data received(" << fetch_id << ")" << response.GetContentString() << " - " << fetch->status << " - " << std::string(fetch->url) << endl;
				cout << "OnFail. Data received(" << fetch_id << ")" << " - " << fetch->status << " - " << std::string(fetch->url) << endl;

				client->currentFetch = nullptr;
				client->currentRequestData = nullptr;
				cout << "OnFail. Fetch null(" << fetch_id << ")" << endl;

				callbacks.erase(fetch_id);
				cout << "OnFail. Calback remove(" << fetch_id << ")" << endl;

				client->onRequestComplete(ec, response);
				cout << "OnFail. OnComplete(" << fetch_id << ")" << endl;

				emscripten_fetch_close(fetch); // Also free data on failure.
				cout << "OnFail. closed(" << fetch_id << ")" << endl;
			}
		}
	}
};

void OnResult(HttpError ec, Response& realResponse);

void callGetRequest(std::string url) {
	EmsFetch* fetch_client = new EmsFetch();

	Request req;
	req.method = "GET";
	req.url = url;
	
	fetch_client->QueryAsync(req, OnResult);
}

void callUsers() {
	EmsFetch* fetch_client = new EmsFetch();

	Request req;
	req.method = "POST";
	req.url = "https://reqres.in/api/users";

	req.headers.insert({ "Content-Type", "application/json" });
	req.headers.insert({ "x-test", "abcdefgh" });
	req.headers.insert({ "x-test_2", "123467890" });
	const char* str_data = u8"{'name': 'morpheus','job' : 'leader'}";
	req.data = str_data;

	fetch_client->QueryAsync(req, OnResult);
}
void callRegister() {
	EmsFetch* fetch_client = new EmsFetch();

	Request req;
	req.method = "POST";
	req.url = "https://reqres.in/api/register";

	req.headers.insert({ "Content-Type", "application/json" });
	req.headers.insert({ "x-test", "abcdefgh" });
	req.headers.insert({ "x-test_2", "123467890" });
	const char* str_data = "{'email': 'morpheus@matrix.com','password' : 'leader'}";
	req.data = str_data;

	fetch_client->QueryAsync(req, OnResult);
}
void callCountry() {
	EmsFetch* fetch_client = new EmsFetch();

	Request req;
	req.method = "POST";
	req.url = "https://get.geojs.io/v1/ip/country";

	const char* str_data = "{'sample_field1': 'sample_data1','sample_field2' : 'sample_data2'}";
	req.data = str_data;

	fetch_client->QueryAsync(req, OnResult);
}

static std::vector<std::string> urls{
	std::string("http://localhost:6931/file_archive.zip"),
	std::string("http://localhost:6931/file_archive2.zip"),
	std::string("http://localhost:6931/file_doc_x.docx"),
	std::string("http://localhost:6931/file_png.png"),
	std::string("http://localhost:6931/file_txt.txt"),
	std::string("http://localhost:6931/file_xml.xml")
};

static int count_reqs = 0;
void callRandomFetch() {

	callGetRequest(urls[count_reqs]);

	count_reqs++;
	if (count_reqs >= urls.size()) {
		count_reqs = 0;
	}
}

void OnResult(HttpError ec, Response& realResponse) {
	//std::cout << "Result received" << ec << " " << realResponse.GetContentString() << endl;
	std::cout << "Result received" << ec << " " << endl;
	emscripten_sleep(2000);
	callRandomFetch();
}


int main()
{
	//http://localhost:6931/file_archive.zip
	//http://localhost:6931/file_archive2.zip
	//http://localhost:6931/file_doc_x.docx
	//http://localhost:6931/file_png.png
	//http://localhost:6931/file_txt.txt
	//http://localhost:6931/file_xml.xml


	//LocalStorageInit();
    std::cout << "Hello World!\n";
	callRandomFetch();
}


