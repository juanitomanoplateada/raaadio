#pragma once
#include <JuceHeader.h>
#include "../database/Database.h"
#include "../library/TrackNaming.h"

// Pedidos y respuestas de AcoustID (qué grabación es una huella de Chromaprint) y MusicBrainz
// (sus datos: artista con invitados, título, año, compositores, ISRC). Lógica pura: arma los
// pedidos y lee las respuestas; quien hace la conexión es OnlineIdentifier.
namespace OnlineMetadata {

// Lo que devuelve AcoustID: grabaciones de MusicBrainz con qué tan seguro está (0 a 1).
struct Candidate {
    double score = 0.0;
    std::string recordingId, title;
    std::vector<std::string> artists;
    double duration = 0.0;  // segundos; 0 si no se sabe
};

juce::String acoustIdUrl();
// Cuerpo del pedido (POST, application/x-www-form-urlencoded).
juce::String acoustIdBody(const juce::String& apiKey, const juce::String& fingerprint, int durationSeconds);
// Candidatos de la respuesta, de más a menos seguros. error: el mensaje del servicio si respondió
// con error (clave inválida, huella mal formada...).
std::vector<Candidate> parseAcoustId(const juce::String& json, juce::String& error);
// El más probable: el de más puntaje; si hay varios casi iguales, el de duración más parecida.
const Candidate* best(const std::vector<Candidate>& candidates, double durationSeconds);

juce::String recordingUrl(const std::string& recordingId);
juce::String userAgent();  // MusicBrainz pide identificarse
// Datos de la grabación: artista principal e invitados (del crédito), título, año del primer
// lanzamiento, compositores de la obra e ISRC.
bool parseRecording(const juce::String& json, Database::OnlineMatch& match);
// Si MusicBrainz no responde, lo que dijo AcoustID alcanza para artista y título.
Database::OnlineMatch fromCandidate(const Candidate& candidate);

// La propuesta de la revisión con estos datos (con los artistas escritos como en la
// biblioteca y la versión separada del título).
TrackNaming::Proposal toProposal(const Database::OnlineMatch& match, const TrackNaming::Context& context);

}
