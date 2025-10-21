#include <FS.h>
#include <WiFiUdp.h>
#include <IntParsing.h>
#include <Settings.h>
#include <MiLightHttpServer.h>
#include <MiLightRadioConfig.h>
#include <string.h>
#include <TokenIterator.h>
#include <AboutHelper.h>
#include <GroupAlias.h>
#include <ProjectFS.h>
#include <StreamUtils.h>

#include <index.html.gz.h>
#include <bundle.css.gz.h>
#include <bundle.js.gz.h>
#include <BackupManager.h>

// ----- Compat ESP32 / PROGMEM / PSTR -----
#if defined(ARDUINO_ARCH_ESP32)
  #ifndef PSTR
    #define PSTR(x) x
  #endif
#endif

// Pour std::bind placeholders (_1, _2, …)
using namespace std::placeholders;

// Chemins HTTP des assets
static const char bundle_css_filename[] = "/bundle.css";
static const char bundle_js_filename[]  = "/bundle.js";

#ifdef ESP32
  #include <SPIFFS.h>
  #include <Update.h>
#endif

void MiLightHttpServer::begin() {
  // /
  server.buildHandler("/")
    .onSimple(HTTP_GET, [&](const UrlTokenBindings*){
      this->handleServe_P(reinterpret_cast<const char*>(index_html_gz),
                          static_cast<size_t>(index_html_gz_len),
                          "text/html");
    });

  // /bundle.css
  server.buildHandler(bundle_css_filename)
    .onSimple(HTTP_GET, [&](const UrlTokenBindings*){
      this->handleServe_P(reinterpret_cast<const char*>(bundle_css_gz),
                          static_cast<size_t>(bundle_css_gz_len),
                          "text/css");
    });

  // /bundle.js
  server.buildHandler(bundle_js_filename)
    .onSimple(HTTP_GET, [&](const UrlTokenBindings*){
      this->handleServe_P(reinterpret_cast<const char*>(bundle_js_gz),
                          static_cast<size_t>(bundle_js_gz_len),
                          "application/javascript");
    });

  server
    .buildHandler("/settings")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::serveSettings, this))
    .on(HTTP_PUT,  std::bind(&MiLightHttpServer::handleUpdateSettings, this, _1))
    .on(
      HTTP_POST,
      std::bind(&MiLightHttpServer::handleUpdateSettingsPost, this, _1),
      std::bind(&MiLightHttpServer::handleUpdateFile,        this, SETTINGS_FILE)
    );

  server
    .buildHandler("/backup")
    .on(HTTP_GET,  std::bind(&MiLightHttpServer::handleCreateBackup, this, _1))
    .on(
        HTTP_POST,
        std::bind(&MiLightHttpServer::handleRestoreBackup, this, _1),
        std::bind(&MiLightHttpServer::handleUpdateFile,    this, BACKUP_FILE));

  server
    .buildHandler("/remote_configs")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleGetRadioConfigs, this, _1));

  server
    .buildHandler("/gateway_traffic")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleListenGateway, this, _1));
  server
    .buildHandler("/gateway_traffic/:type")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleListenGateway, this, _1));

  server
    .buildHandler("/gateways/:device_id/:type/:group_id")
    .on(HTTP_PUT,    std::bind(&MiLightHttpServer::handleUpdateGroup, this, _1))
    .on(HTTP_POST,   std::bind(&MiLightHttpServer::handleUpdateGroup, this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteGroup, this, _1))
    .on(HTTP_GET,    std::bind(&MiLightHttpServer::handleGetGroup,    this, _1));

  server
    .buildHandler("/gateways/:device_alias")
    .on(HTTP_PUT,    std::bind(&MiLightHttpServer::handleUpdateGroupAlias, this, _1))
    .on(HTTP_POST,   std::bind(&MiLightHttpServer::handleUpdateGroupAlias, this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteGroupAlias, this, _1))
    .on(HTTP_GET,    std::bind(&MiLightHttpServer::handleGetGroupAlias,    this, _1));

  server
    .buildHandler("/gateways")
    .onSimple(HTTP_GET, [&](const UrlTokenBindings*){ this->handleListGroups(); })
    .on(HTTP_PUT, std::bind(&MiLightHttpServer::handleBatchUpdateGroups, this, _1));

  server
    .buildHandler("/transitions/:id")
    .on(HTTP_GET,    std::bind(&MiLightHttpServer::handleGetTransition,    this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteTransition, this, _1));

  server
    .buildHandler("/transitions")
    .on(HTTP_GET,  std::bind(&MiLightHttpServer::handleListTransitions, this, _1))
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleCreateTransition, this, _1));

  server
    .buildHandler("/raw_commands/:type")
    .on(HTTP_ANY, std::bind(&MiLightHttpServer::handleSendRaw, this, _1));

  server
    .buildHandler("/about")
    .on(HTTP_GET, std::bind(&MiLightHttpServer::handleAbout, this, _1));

  server
    .buildHandler("/system")
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleSystemPost, this, _1));

  server
    .buildHandler("/aliases")
    .on(HTTP_GET,  std::bind(&MiLightHttpServer::handleListAliases,  this, _1))
    .on(HTTP_POST, std::bind(&MiLightHttpServer::handleCreateAlias, this, _1));

  server
    .buildHandler("/aliases.bin")
    .on(HTTP_GET,    std::bind(&MiLightHttpServer::serveFile,        this, ALIASES_FILE, APPLICATION_OCTET_STREAM))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteAliases, this, _1))
    .on(
        HTTP_POST,
        std::bind(&MiLightHttpServer::handleUpdateAliases, this, _1),
        std::bind(&MiLightHttpServer::handleUpdateFile,    this, ALIASES_FILE)
    );

  server
    .buildHandler("/aliases/:id")
    .on(HTTP_PUT,    std::bind(&MiLightHttpServer::handleUpdateAlias, this, _1))
    .on(HTTP_DELETE, std::bind(&MiLightHttpServer::handleDeleteAlias, this, _1));

  server
    .buildHandler("/firmware")
    .handleOTA();

  server.clearBuilders();

  // WebSocket
  wsServer.onEvent(
    [this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
      handleWsEvent(num, type, payload, length);
    }
  );
  wsServer.begin();

  server.begin();
}

void MiLightHttpServer::onAbout(AboutHandler handler) {
  this->aboutHandler = handler;
}

void MiLightHttpServer::handleClient() {
  server.handleClient();
  wsServer.loop();
}

WiFiClient MiLightHttpServer::client() {
  return server.client();
}

void MiLightHttpServer::on(const char *path, HTTPMethod method, THandlerFunction handler) {
  server.on(path, method, handler);
}

// ------------------------------------------------------------------
//  Divers handlers (seuls changements: printf_P → printf / print)
// ------------------------------------------------------------------

void MiLightHttpServer::handleWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      if (numWsClients > 0) {
        numWsClients--;
      }
      break;

    case WStype_CONNECTED:
      numWsClients++;
      break;

    default:
      Serial.printf("Unhandled websocket event: %d\n", static_cast<uint8_t>(type));
      break;
  }
}

void MiLightHttpServer::handleCreateBackup(RequestContext &request) {
  File backupFile = ProjectFS.open(BACKUP_FILE, "w");

  if (!backupFile) {
    Serial.println(F("Failed to open backup file"));
    request.response.setCode(500);
    request.response.json[F("error")] = F("Failed to open backup file");
  }

  WriteBufferingStream bufferedStream(backupFile, 64);
  BackupManager::createBackup(settings, bufferedStream);
  bufferedStream.flush();
  backupFile.close();

  backupFile = ProjectFS.open(BACKUP_FILE, "r");
  Serial.printf("Sending backup file of size %d\n", (int)backupFile.size());
  server.streamFile(backupFile, APPLICATION_OCTET_STREAM);

  ProjectFS.remove(BACKUP_FILE);
}

void MiLightHttpServer::handleListGroups() {
  this->stateStore->flush();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json");

  StaticJsonDocument<1024> stateBuffer;
  WiFiClient client = server.client();

  // open array
  server.sendContent("[");

  bool firstGroup = true;
  for (auto & group : settings.groupIdAliases) {
    stateBuffer.clear();

    JsonObject device = stateBuffer.createNestedObject(F("device"));

    device[F("alias")]      = group.first;
    device[F("id")]         = group.second.id;
    device[F("device_id")]  = group.second.bulbId.deviceId;
    device[F("group_id")]   = group.second.bulbId.groupId;
    device[F("device_type")] = MiLightRemoteTypeHelpers::remoteTypeToString(group.second.bulbId.deviceType);
    
    GroupState* state = this->stateStore->get(group.second.bulbId);
    JsonObject outputState = stateBuffer.createNestedObject(F("state"));

    if (state != nullptr) {
      state->applyState(outputState, group.second.bulbId, NORMALIZED_GROUP_STATE_FIELDS);
    }

    client.printf("%zx\r\n", measureJson(stateBuffer) + (firstGroup ? 0 : 1));
    if (!firstGroup) {
      client.print(',');
    }
    serializeJson(stateBuffer, client);
    client.print("\r\n");

    firstGroup = false;
    yield();
  }

  // close array
  server.sendContent("]");

  // stop chunked streaming
  server.sendContent("");
  server.client().stop();
}

// ------------------------------------------------------------------
//  Envoi d'un buffer gzip stocké en PROGMEM (chunked) – signature
//  identique au .h : const char* + size_t + contentType
// ------------------------------------------------------------------
void MiLightHttpServer::handleServe_P(const char* data,
                                      size_t length,
                                      const char* contentType) {
  const size_t CHUNK_SIZE = 4096;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "public, max-age=31536000");
  server.send(200, contentType);

  WiFiClient client = server.client();

  const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
  size_t remaining = length;

  while (remaining > 0) {
    size_t chunk = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;

    // taille du chunk (hex)
    client.printf("%X\r\n", (unsigned)chunk);

    // données
    client.write(p, chunk);
    client.print("\r\n");

    p         += chunk;
    remaining -= chunk;
  }

  // chunk terminal
  client.print("0\r\n\r\n");
  client.stop();
}
// ------------------------------------------------------------------
//  Suppression d'un alias — STUB neutre pour ESP32 (API bindings variable)
//  -> Répond 501 afin de laisser le firmware compiler et tourner
// ------------------------------------------------------------------
void MiLightHttpServer::handleDeleteAlias(RequestContext& request) {
  request.response.setCode(501); // Not Implemented
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("delete alias not implemented for this build");
}
// ===================================================================
// STUBS MINIMAUX POUR FINALISER LA COMPILATION / LIEN
// (Implémentations neutres des méthodes référencées dans begin())
// ===================================================================

void MiLightHttpServer::onSettingsSaved(std::function<void ()> cb) {
  // Stub: on ignore le callback pour ce build
  (void)cb;
}

void MiLightHttpServer::onGroupDeleted(std::function<void (const BulbId&)> cb) {
  // Stub: on ignore le callback pour ce build
  (void)cb;
}

void MiLightHttpServer::handlePacketSent(
    unsigned char* /*packet*/,
    const MiLightRemoteConfig& /*remoteConfig*/,
    const BulbId& /*bulbId*/,
    const ArduinoJson::JsonObject& /*result*/
) {
  // Stub: rien à faire
}

// --------- GET /settings ----------
void MiLightHttpServer::serveSettings() {
  // Réponse minimaliste (vide) pour que l’UI ne plante pas
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json");
  server.sendContent("{}");
  server.sendContent("");
  server.client().stop();
}

// --------- PUT /settings ----------
void MiLightHttpServer::handleUpdateSettings(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleUpdateSettings not implemented");
}

// --------- POST /settings (form/upload) ----------
void MiLightHttpServer::handleUpdateSettingsPost(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleUpdateSettingsPost not implemented");
}

// --------- Upload générique d’un fichier ----------
void MiLightHttpServer::handleUpdateFile(const char* /*path*/) {
  // Cette méthode est souvent utilisée comme handler d’upload binaire.
  // Stub no-op : rien à faire ici pour compiler.
}

// --------- POST /backup (restore) ----------
void MiLightHttpServer::handleRestoreBackup(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleRestoreBackup not implemented");
}

// --------- GET /remote_configs ----------
void MiLightHttpServer::handleGetRadioConfigs(RequestContext& request) {
  request.response.setCode(200);
  request.response.json.createNestedArray(F("remotes")); // vide
}

// --------- GET /gateway_traffic[/:type] ----------
void MiLightHttpServer::handleListenGateway(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleListenGateway not implemented");
}

// --------- /gateways/:device_id/:type/:group_id ----------
void MiLightHttpServer::handleUpdateGroup(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleUpdateGroup not implemented");
}

void MiLightHttpServer::handleDeleteGroup(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleDeleteGroup not implemented");
}

void MiLightHttpServer::handleGetGroup(RequestContext& request) {
  request.response.setCode(200);
  request.response.json[F("state")] = ArduinoJson::VariantConst(); // null
}

// --------- /gateways/:device_alias ----------
void MiLightHttpServer::handleUpdateGroupAlias(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleUpdateGroupAlias not implemented");
}

void MiLightHttpServer::handleDeleteGroupAlias(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleDeleteGroupAlias not implemented");
}

void MiLightHttpServer::handleGetGroupAlias(RequestContext& request) {
  request.response.setCode(200);
  request.response.json[F("alias")] = ArduinoJson::VariantConst(); // null
}

// --------- /gateways (batch PUT) ----------
void MiLightHttpServer::handleBatchUpdateGroups(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleBatchUpdateGroups not implemented");
}

// --------- /transitions/:id ----------
void MiLightHttpServer::handleGetTransition(RequestContext& request) {
  request.response.setCode(200);
  request.response.json[F("transition")] = ArduinoJson::VariantConst(); // null
}

void MiLightHttpServer::handleDeleteTransition(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleDeleteTransition not implemented");
}

// --------- /transitions ----------
void MiLightHttpServer::handleListTransitions(RequestContext& request) {
  request.response.setCode(200);
  request.response.json.createNestedArray(F("transitions")); // vide
}

void MiLightHttpServer::handleCreateTransition(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleCreateTransition not implemented");
}

// --------- /raw_commands/:type ----------
void MiLightHttpServer::handleSendRaw(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleSendRaw not implemented");
}

// --------- /about ----------
void MiLightHttpServer::handleAbout(RequestContext& request) {
  request.response.setCode(200);
  request.response.json[F("version")] = F("custom-esp32-build");
}

// --------- /system (POST) ----------
void MiLightHttpServer::handleSystemPost(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleSystemPost not implemented");
}

// --------- /aliases (GET/POST) ----------
void MiLightHttpServer::handleListAliases(RequestContext& request) {
  request.response.setCode(200);
  request.response.json.createNestedArray(F("aliases")); // vide
}

void MiLightHttpServer::handleCreateAlias(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleCreateAlias not implemented");
}

// --------- /aliases.bin (GET) ----------
void MiLightHttpServer::serveFile(const char* path, const char* contentType) {
  File f = ProjectFS.open(path, "r");
  if (!f) {
    server.send(404, "application/json", "{\"error\":\"file not found\"}");
    return;
  }
  server.streamFile(f, contentType ? contentType : APPLICATION_OCTET_STREAM);
  f.close();
}

// --------- /aliases.bin (DELETE/POST) ----------
void MiLightHttpServer::handleDeleteAliases(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleDeleteAliases not implemented");
}

void MiLightHttpServer::handleUpdateAliases(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleUpdateAliases not implemented");
}

// --------- /aliases/:id (PUT) ----------
void MiLightHttpServer::handleUpdateAlias(RequestContext& request) {
  request.response.setCode(501);
  request.response.json[F("ok")] = false;
  request.response.json[F("error")] = F("handleUpdateAlias not implemented");
}
