#include <cryptopp/base64.h>
#include <cryptopp/hex.h>

#include "cmclient.h"
#include "content_manifest.pb.h"

#include <iostream>

const auto PROTOBUF_PAYLOAD_MAGIC = 0x71F617D0;
const auto PROTOBUF_METADATA_MAGIC = 0x1F4812BE;
const auto PROTOBUF_SIGNATURE_MAGIC = 0x1B81B817;

struct CSClientPool::CSClient {
	std::string uri;
	unsigned char sessionKey[32];
	bool ready;
	std::uint32_t depotid;
	std::uint64_t req_counter;
	std::uint64_t session_id;
};

CSClientPool::CSClientPool(std::vector<std::string> servers, const unsigned char* app_ticket, std::size_t app_ticket_length, http_request_t http_request) : http_request(std::move(http_request)) {
	for (auto &server : servers) {
		auto client = new CSClient();
		clients.push_back(client);

		client->ready = false; // MSVC is garbage

		AutoSeededRandomPool rnd;

		client->uri = std::move(server);

		std::vector<unsigned char> crypted_sess_key(128);
		create_session_key(rnd, client->sessionKey, crypted_sess_key.data());

		std::vector<unsigned char> crypted_ticket(crypted_length(app_ticket_length));
		symmetric_encrypt(rnd, client->sessionKey, app_ticket, app_ticket_length, crypted_ticket.data());

		std::vector<std::pair<const char*, const char*>> headers;
		headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");

		std::vector<std::tuple<const char*, const unsigned char*, std::size_t>> query;
		query.emplace_back("sessionkey", crypted_sess_key.data(), crypted_sess_key.size());
		query.emplace_back("appticket", crypted_ticket.data(), crypted_ticket.size());

		// debug
		std::cout << "start initsession with " << client->uri << '\n';

		this->http_request(
			("http://" + client->uri + "/initsession/").c_str(),
			"POST",
			headers,
			query,
			[this, client](int status_code, std::string &body) {
				/*
				"response"
				{
					"sessionid"		"2756818007967047057"
					"req-counter"		"0"
					"csid"		"53"
				}
				*/
				std::cout << "complete initsession with " << client->uri << status_code << '\n';
				if (status_code != 200) {
					clients.erase(std::find(clients.begin(), clients.end(), client));
					return;
				}
				auto response = *(*body)["response"];
				client->session_id = std::stoll(response["sessionid"]);
				client->req_counter = std::stoull(response["req-counter"]);
				client->ready = true;
				if (!queue.empty()) {
					std::cout << "queue not empty... " << client->uri <<  '\n';
					queue.front()(*client);
					queue.pop();
				}
			}
		);
	}
}

std::string CSClientPool::make_header(CSClient &client, const std::string &uri) {
	client.req_counter++;

	SHA1 sha;
	sha.Update(reinterpret_cast<const byte *>(&client.session_id), sizeof client.session_id);
	sha.Update(reinterpret_cast<const byte *>(&client.req_counter), sizeof client.req_counter);
	sha.Update(client.sessionKey, sizeof client.sessionKey);
	sha.Update(reinterpret_cast<const byte *>(uri.c_str()), uri.length());

	byte digest[SHA1::DIGESTSIZE];
	sha.Final(digest);

	HexEncoder encoder;
	std::string output;
	encoder.Attach(new StringSink(output));
	encoder.Put(digest, sizeof(digest));
	encoder.MessageEnd();

	return "sessionid=" + std::to_string(client.session_id) + ";req-counter=" + std::to_string(client.req_counter) + ";hash=" + output + ";";
}

void CSClientPool::DoOrQueue(std::uint32_t depotid, std::string app_ticket, std::string uri, std::function<void(std::string body)> callback) {
	auto once_authdepot = [=](CSClient &client) {
		auto path = "/depot/" + std::to_string(depotid) + uri;
		auto auth = make_header(client, path);

		std::vector<std::pair<const char*, const char*>> headers;
		headers.emplace_back("x-steam-auth", auth.c_str());

		http_request(
			("http://" + client.uri + path).c_str(),
			"GET",
			headers,
			std::vector<std::tuple<const char*, const unsigned char*, std::size_t>>(),
			[this, &client, callback](int status_code, std::string body) {
				assert(status_code == 200);
				
				callback(body);

				if (!queue.empty()) {
					queue.front()(client);
					queue.pop();
				} else {
					client.ready = true;
				}
			}
		);
	};

	auto once_ready = [this, depotid, app_ticket, once_authdepot](CSClient &client) {
		client.ready = false;

		if (client.depotid != depotid) {
			// must authdepot

			std::vector<unsigned char> crypted_ticket(crypted_length(app_ticket.length()));
			symmetric_encrypt(AutoSeededRandomPool(), client.sessionKey, reinterpret_cast<const unsigned char*>(app_ticket.data()), app_ticket.length(), crypted_ticket.data());

			auto uri = "/authdepot/";
			auto auth = make_header(client, uri);
			
			std::vector<std::pair<const char*, const char*>> headers;
			headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");
			headers.emplace_back("x-steam-auth", auth.c_str());

			std::vector<std::tuple<const char*, const unsigned char*, std::size_t>> query;
			query.emplace_back("appticket", crypted_ticket.data(), crypted_ticket.size());
			
			http_request(
				("http://" + client.uri + uri).c_str(),
				"POST",
				headers,
				query,
				[&client, depotid, once_authdepot](int status_code, std::string &body) {
					assert(status_code == 200);
					client.depotid = depotid;
					once_authdepot(client);
				}
			);
		} else {
			once_authdepot(client);
		}
	};

	std::vector<CSClient*> ready_clients;
	std::copy_if(clients.begin(), clients.end(), std::back_inserter(ready_clients), [](CSClient* const client) {
		return client->ready;
	});
	if (ready_clients.size()) {
		// prefer clients authdepotted for required depotid
		auto authdepotted = std::find_if(ready_clients.begin(), ready_clients.end(), [depotid](CSClient* const client) {
			return client->depotid == depotid;
		});
		once_ready(authdepotted == ready_clients.end() ? *ready_clients[0] : **authdepotted);
	} else {
		queue.emplace(std::move(once_ready));
	}
}

void CSClientPool::DownloadDepotManifest(
	std::uint32_t depotid,
	std::uint64_t manifestid,
	std::string app_ticket,
	std::string depot_key,
	std::function<void(std::vector<FileData> &files)> callback
) {
	DoOrQueue(depotid, app_ticket, "/manifest/" + std::to_string(manifestid) + "/5", [depot_key, callback](std::string body) {
		body = unzip(body);
		auto data = body.data();

		assert(*reinterpret_cast<const uint32_t*>(data) == PROTOBUF_PAYLOAD_MAGIC);
		auto payload_len = *reinterpret_cast<const uint32_t*>(data + 4);
		data += 8;

		ContentManifestPayload payload;
		payload.ParseFromArray(data, payload_len);

		std::vector<FileData> files;
		files.reserve(payload.mappings_size());

		for (auto &file : *payload.mutable_mappings()) {
			std::vector<ChunkData> chunks;// (file.chunks_size());
			chunks.reserve(file.chunks_size());

			for (auto &chunk : *file.mutable_chunks()) {
				chunks.push_back({ chunk.sha(), chunk.offset(), chunk.cb_original() });
			}

			std::string filename;
			StringSource(file.filename(), true, new Base64Decoder(new StringSink(filename)));

			assert(depot_key.size() == 32);
					
			filename = symmetric_decrypt(
				reinterpret_cast<const unsigned char*>(depot_key.data()),
				reinterpret_cast<const unsigned char*>(filename.data()),
				filename.size()
			);

			filename.pop_back(); // useless null char
			std::replace(filename.begin(), filename.end(), '\\', '/');

			FileData data = {std::move(filename), chunks, static_cast<EDepotFileFlag>(file.flags()), file.size()};
			files.emplace_back(std::move(data));
		}

		//for (auto &f : files) {
		//	std::cout << f;
		//} std::cout << std::endl;
		callback(files);

		// TODO: look at other two parts?
	});
}

void CSClientPool::DownloadDepotChunk(
	std::uint32_t depotid,
	std::string chunkid,
	std::string app_ticket,
	std::string depot_key,
	std::function<void(std::string chunk)> callback
) {
	std::string encoded;
	encoded.reserve(40);
	StringSource ss(chunkid, true, new HexEncoder(new StringSink(encoded)));
	DoOrQueue(depotid, app_ticket, "/chunk/" + encoded, [=](std::string chunk) {
		auto decrypted_chunk = symmetric_decrypt(
			reinterpret_cast<const unsigned char*>(depot_key.data()),
			reinterpret_cast<const unsigned char*>(chunk.data()),
			chunk.size()
		);
		callback(unzip(decrypted_chunk));
	});
}

void CSClientPool::FetchServerList(
	const char* host,
	std::uint16_t port,
	int cell_id,
	const http_request_t &http_request,
	std::function<void(std::vector<std::string> &server_list)> callback
) {
	http_request(
		("http://" + (host + (":" + std::to_string(port) + "/serverlist/" + std::to_string(cell_id) + "/6/"))).c_str(),
		"GET",
		std::vector<std::pair<const char*, const char*>>(),
		std::vector<std::tuple<const char*, const unsigned char*, std::size_t>>(),
		[callback](int status_code, std::string &body) {
			auto servers = parse_VDF(parse_VDF(body)["serverlist"]);
			std::vector<std::string> CSs;
			for (auto &kv : servers) {
				/*
				"0"
				{
					"type"		"CS"
					"sourceid"		"33"
					"cell"		"66"
					"load"		"63"
					"weightedload"		"63"
					"host"		"valve233.cs.steampowered.com"
					"vhost"		"valve233.cs.steampowered.com"
				}
				*/
				auto server = parse_VDF(kv.second);
				if (server["type"] == "CS")
					CSs.push_back(server["host"]);
			}
			callback(CSs);
		}
	);
}
