#include "HttpTransport.h"

namespace HttpTransport {

Response send(const Request& request) {
    // Sin separar los parámetros: en un POST, JUCE los pasaría de la dirección al cuerpo
    // ("?prettyPrint=false" delante del JSON de YouTube).
    auto url = juce::URL::createWithoutParsing(request.url);
    const bool post = request.body.isNotEmpty();
    if (post) url = url.withPOSTData(request.body);

    juce::WebInputStream stream(url, post);
    stream.withExtraHeaders(request.headers).withConnectionTimeout(15000).withNumRedirectsToFollow(3);
    Response response;
    if (!stream.connect(nullptr)) return response;
    response.status = stream.getStatusCode();
    if (request.binary) stream.readIntoMemoryBlock(response.data);
    else response.body = stream.readEntireStreamAsString();
    return response;
}

bool downloadToFile(const juce::String& url, const juce::File& target, const Progress& progress) {
    juce::WebInputStream stream(juce::URL::createWithoutParsing(url), false);
    stream.withConnectionTimeout(20000).withNumRedirectsToFollow(5);  // GitHub redirige a su almacenamiento
    if (!stream.connect(nullptr) || stream.getStatusCode() != 200) return false;

    const auto partial = target.getSiblingFile(target.getFileName() + ".part");
    partial.deleteFile();
    target.getParentDirectory().createDirectory();
    bool ok = true;
    {
        juce::FileOutputStream out(partial);
        if (!out.openedOk()) return false;
        const auto total = stream.getTotalLength();
        juce::int64 done = 0;
        juce::HeapBlock<char> buffer(65536);
        while (!stream.isExhausted()) {
            const int n = stream.read(buffer.get(), 65536);
            if (n < 0 || (n == 0 && stream.isError())) { ok = false; break; }
            if (n == 0) break;
            if (!out.write(buffer.get(), (size_t) n)) { ok = false; break; }
            done += n;
            if (progress && !progress(total > 0 ? (double) done / (double) total : -1.0)) { ok = false; break; }
        }
        ok = ok && (total <= 0 || done == total);
        out.flush();
        ok = ok && !out.getStatus().failed();
    }
    if (ok) {
        target.deleteFile();
        ok = partial.moveFileTo(target);
    }
    if (!ok) partial.deleteFile();
    return ok;
}

}
