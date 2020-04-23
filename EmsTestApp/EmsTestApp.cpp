// EmsTestApp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <map>
#include <emscripten/fetch.h>
#include <emscripten.h>

using namespace std;

static bool cuurent_choice = false;
class EmsFetch;
static std::map<uint32_t, EmsFetch*> callbacks;

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
};

typedef void(*HttpRequestHandler)(HttpError ec, Response& realResponse);
typedef void(*HttpHeadersHandler)(HttpError ec, std::map<std::string, std::string> headers);

class EmsFetch
{
public:
	HttpHeadersHandler onHeadersReceived;
	HttpRequestHandler onRequestComplete;

	emscripten_fetch_t* currentFetch = nullptr;

	void OnReadHeaders(const HttpHeadersHandler& handler, HttpError ec) {
		if (IsRunning()) {
			ec = HttpError::AlreadyStarted;
			return;
		}

		ec = HttpError::None;

		onHeadersReceived = handler;
	}

	void QueryAsync(const Request& request, const HttpRequestHandler &handler) {
		onRequestComplete = handler;

		std::string content;

		std::cout << "Start async " << request.url.c_str() << endl;

		// определение параметров заголовка запроса
		const char *method = nullptr;

		method = request.method.c_str();
		if (request.method == "POST") {
			content = request.data;
		}

		emscripten_fetch_attr_t attr;
		emscripten_fetch_attr_init(&attr);

		strcpy(attr.requestMethod, method);
		attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_APPEND;

		char* req_data = (char*)content.c_str();

		attr.requestData = req_data;
		attr.requestDataSize = strlen(attr.requestData);

		cout << "Set request data(size: " << attr.requestDataSize << "): " << attr.requestData << endl;

		int size = request.headers.size() * 2 + 1;
		char** custom_headers = new char*[size];

		int count = 0;
		for (const auto &header : request.headers) {
			custom_headers[count++] = (char*)header.first.c_str();
			custom_headers[count++] = (char*)header.second.c_str();
			cout << "Add header: " << custom_headers[count - 2] << ": " << custom_headers[count - 1] << endl;
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

		cout << "Start fetch(" << currentFetch->id << "): " << currentFetch->url << endl;
	}

	void CallAsync(const Request& httpRequest) {
	}

	void Cancel() {
		if (IsRunning()) {
			cout << "Cancel fetch(" << currentFetch->id << "): " << currentFetch->url << endl;
			callbacks[currentFetch->id] = nullptr;
			//TODO: send Abort to callback
			emscripten_fetch_close(currentFetch);
		}

		currentFetch = nullptr;
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
	}

	static void OnProgress(emscripten_fetch_t *fetch) {
		//Process the partial data stream fetch->data[0] thru fetch->data[fetch->numBytes-1]
		// This buffer represents the file at offset fetch->dataOffset.
		cout << "(" << fetch->id << ") Num bytes: " << fetch->numBytes << endl;
		cout << "(" << fetch->id << ") Data offset: " << fetch->dataOffset << endl;
		cout << "(" << fetch->id << ") Total bytes: " << fetch->totalBytes << endl;
		cout << "(" << fetch->id << ") Calc : " << (fetch->dataOffset + fetch->numBytes) * 100.0 / fetch->totalBytes << endl;

		//for (size_t i = 0; i < fetch->numBytes; ++i)
		//{
			//Process fetch->data[i];
		//}
	}

	static HttpHeaders GetHeaders(emscripten_fetch_t *fetch) {
		if (fetch->readyState != 2) {
			return HttpHeaders();
		}

		HttpHeaders headers = HttpHeaders();

		size_t headersLengthBytes = emscripten_fetch_get_response_headers_length(fetch) + 1;
		char *headerString = new char[headersLengthBytes];

		emscripten_fetch_get_response_headers(fetch, headerString, headersLengthBytes);
		cout << "GetHeaders. (" << fetch->id << "): " << " , url = " << fetch->url << " , response: " << headerString << endl;

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

				cout << "OnHeaders. Headers(" << fetch->id << "): " << headers.size() << ", last-mod: " << " , url = " << fetch->url << endl;

				client->onHeadersReceived(HttpError::None, headers);
			}
		}
	}

	static void OnSuccess(emscripten_fetch_t *fetch) {
		uint32_t fetch_id = fetch->id;
		cout << "OnSuccess. Finished downloading(" << fetch_id << "): " << fetch->numBytes << " bytes from URL: " << fetch->url << endl;

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
				cout << "OnSuccess. Data fetch(" << fetch_id << "): " << (char*)response.resp_data << " - " << fetch->status << " - " << fetch->url << endl;

				response.resp_data_headers = (GetHeaders(fetch));

				callbacks.erase(fetch_id);
				cout << "OnSuccess. Remove request info(" << fetch_id << ")" << endl;
				client->currentFetch = nullptr;
				cout << "OnSuccess. Close(" << fetch_id << ")" << endl;
				client->onRequestComplete(ec, response);
				cout << "OnSuccess. OnComplete(" << fetch_id << ")" << endl;

				emscripten_fetch_close(fetch); // Free data associated with the fetch.
			}
		}
	}

	static void OnFail(emscripten_fetch_t *fetch) {
		uint32_t fetch_id = fetch->id;
		cout << "OnFail. Fetch(" << fetch_id << "): " << fetch->url << " failed, HTTP failure status code: " << fetch->status << endl;

		if (IsRequestActive(fetch_id)) {
			EmsFetch* client = (EmsFetch*)callbacks[fetch_id];

			if (client->onRequestComplete != nullptr) {
				HttpError ec = HttpError::BadRequest;
				Response response = Response();

				// The data is now available at fetch->data[0] through fetch->data[fetch->numBytes-1];
				response.responseCode = (fetch->status);
				response.resp_data = (const uint8_t*)fetch->data;
				response.resp_data_length = fetch->numBytes;

				cout << "OnFail. Data received(" << fetch_id << ")" << (char*)response.resp_data << " - " << fetch->status << " - " << fetch->url << endl;

				client->currentFetch = nullptr;
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

void callRandomFetch() {
	EmsFetch* fetch_client = new EmsFetch();

	Request req;
	req.method = "POST";
	req.url = cuurent_choice ? "https://web.playrix.com/nodeapps/404" : "https://web.playrix.com/nodeapps/hsfb-dev/api/Auth";

	cuurent_choice = !cuurent_choice;

	req.headers.insert({ "x-test", "abcdefgh" });
	req.headers.insert({ "x-test_2", "123467890" });
	req.data = "{'test':'test_json', 'test_2':'TEST:DEBUG'}";

	fetch_client->QueryAsync(req, OnResult);
}

void OnResult(HttpError ec, Response& realResponse) {
	std::cout << realResponse.resp_data;
	emscripten_sleep(2000);
	callRandomFetch();
}

int main()
{
    std::cout << "Hello World!\n";

	callRandomFetch();
}


