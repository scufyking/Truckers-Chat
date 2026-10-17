// Trucker Chat - unit tests for the platform-independent code (build and run on Linux/macOS/Windows)
// Copyright (c) 2026 ScufyKing
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "TextUtil.h"
#include "Filter.h"
#include "TwitchProtocol.h"
#include "YouTubeProtocol.h"
#include "Canvas.h"
#include <cstdio>

static int g_fail = 0, g_ok = 0;
#define CHECK(c) do { if (c) ++g_ok; else { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// fixed-width fake glyphs: every character is 10 px wide; face 0 has ASCII + Latin-1, face 1 has CJK
struct FakeGlyphs : GlyphBackend {
    int FaceCount() const override { return 2; }
    bool HasGlyph(int f, char32_t c) override { return f == 0 ? (c >= 0x20 && c < 0x250) || c == 0x2026 : (c >= 0x3000 && c < 0xA000); }
    void Metrics(int, const Font& f, int& a, int& d) override { a = f.px * 8 / 10; d = f.px / 5; }
    int Measure(int, const Font&, const std::u32string& r) override { return (int)r.size() * 10; }
    void Draw(int, const Font&, const std::u32string&, int, int, std::vector<uint8_t>&, int, int) override {}
};

static void TestUtf8() {
    CHECK(DecodeUtf8("abc") == U"abc");
    CHECK(DecodeUtf8("\xC3\xA4") == U"ä");
    CHECK(DecodeUtf8("\xF0\x9F\x9A\x9A") == U"\U0001F69A");
    CHECK(DecodeUtf8("\xC0\xAF") == U"��");          // overlong
    CHECK(DecodeUtf8("\xED\xA0\x80")[0] == U'�');          // surrogate
    CHECK(DecodeUtf8("\xE2\x82") == U"��");           // truncated
    CHECK(EncodeUtf8(U"ä\U0001F69A") == "\xC3\xA4\xF0\x9F\x9A\x9A");
    CHECK(ToUtf16(U"\U0001F69A").size() == 2);
    CHECK(Lower(U"ÄBCД") == U"äbcд");
}

static void TestSanitize() {
    SanitizeOptions o;
    CHECK(SanitizeText(U"  hallo \t\n welt  ", o) == U"hallo welt");
    CHECK(SanitizeText(U"a‮b​c", o) == U"abc");                       // bidi override + zero width removed
    CHECK(SanitizeText(U"é́́́́", o) == U"é́");  // Zalgo capped at 2 marks
    CHECK(SanitizeText(U"\U0001F44D\U0001F3FD️", o) == U"\U0001F44D");     // skin tone + VS16 dropped
    o.emoji = "remove";
    CHECK(SanitizeText(U"gute fahrt \U0001F69A", o) == U"gute fahrt");
    o.emoji = "replace"; o.emojiReplacement = U"*";
    CHECK(SanitizeText(U"x \U0001F69A\U0001F69A\U0001F69A\U0001F69A\U0001F69A", o) == U"x ***");
    o.emoji = "keep"; o.maxLength = 5;
    CHECK(SanitizeText(U"abcdefgh", o) == U"abcd…");
    CHECK(SanitizeName(U"ㅤㅤ") == U"");                                 // invisible filler name
}

static void TestFilter() {
    FilterConfig fc;
    fc.ignoreUsers = {"Nightbot"};
    fc.blockedWords = {"Spoiler"};
    MessageFilter f; f.Configure(fc);
    ChatMessage m; m.platform = Platform::Twitch; m.user = "Anna"; m.login = "anna";
    m.text = "!song"; CHECK(!f.Apply(m));
    m.text = "  !discord"; CHECK(!f.Apply(m));
    m.text = "hallo"; CHECK(f.Apply(m));
    m.text = "kein SPOILER bitte"; CHECK(!f.Apply(m));
    ChatMessage b = m; b.text = "hi"; b.login = "nightbot"; b.user = "Nightbot"; CHECK(!f.Apply(b));
    m.text = "schau https://example.com/x jetzt"; CHECK(f.Apply(m)); CHECK(m.text == "schau [Link] jetzt");
    fc.blockedWordsAction = "mask"; f.Configure(fc);
    m.text = "das ist ein spoiler!"; CHECK(f.Apply(m)); CHECK(m.text == "das ist ein *******!");
    fc.links = "hide"; f.Configure(fc);
    m.text = "www.example.com"; CHECK(!f.Apply(m));
    m.text = "awww.nice"; CHECK(f.Apply(m));
}

static void TestTwitch() {
    IrcMessage m;
    CHECK(ParseIrcLine("@badge-info=;badges=broadcaster/1;color=#0000FF;display-name=Scufy\\sKing;id=abc-1;mod=0;user-id=42 "
                       ":scufyking!scufyking@scufyking.tmi.twitch.tv PRIVMSG #scufyking :Hallo Chat; wie geht's?\r\n", m));
    CHECK(m.command == "PRIVMSG");
    CHECK(m.params.size() == 2 && m.params[1] == "Hallo Chat; wie geht's?");
    TwitchResult r = InterpretTwitch(m);
    CHECK(r.kind == TwitchResult::Kind::Event);
    CHECK(r.event.msg.user == "Scufy King");
    CHECK(r.event.msg.login == "scufyking");
    CHECK(r.event.msg.color == 0x0000FF && r.event.msg.hasColor);
    CHECK(r.event.msg.owner && r.event.msg.id == "abc-1" && r.event.msg.userId == "42");
    CHECK(ParseIrcLine(":a!a@a PRIVMSG #c :\x01" "ACTION winkt\x01", m));
    r = InterpretTwitch(m);
    CHECK(r.event.msg.action && r.event.msg.text == "winkt");
    CHECK(ParseIrcLine("PING :tmi.twitch.tv", m)); r = InterpretTwitch(m);
    CHECK(r.kind == TwitchResult::Kind::Ping && r.text == "tmi.twitch.tv");
    CHECK(ParseIrcLine("@login=x;target-msg-id=abc-1 :tmi.twitch.tv CLEARMSG #c :bad", m)); r = InterpretTwitch(m);
    CHECK(r.kind == TwitchResult::Kind::Event && r.event.kind == ChatEvent::Kind::DeleteMessage && r.event.key == "abc-1");
    CHECK(ParseIrcLine("@ban-duration=600;target-user-id=99 :tmi.twitch.tv CLEARCHAT #c :troll", m)); r = InterpretTwitch(m);
    CHECK(r.event.kind == ChatEvent::Kind::DeleteUser && r.event.key == "99");
    CHECK(ParseIrcLine(":tmi.twitch.tv CLEARCHAT #c", m)); r = InterpretTwitch(m);
    CHECK(r.event.kind == ChatEvent::Kind::ClearAll);
    CHECK(ParseIrcLine(":tmi.twitch.tv NOTICE * :Login authentication failed", m)); r = InterpretTwitch(m);
    CHECK(r.kind == TwitchResult::Kind::AuthFailed);
    CHECK(ParseIrcLine(":tmi.twitch.tv RECONNECT", m)); CHECK(InterpretTwitch(m).kind == TwitchResult::Kind::Reconnect);
    CHECK(NormalizeTwitchChannel("https://www.twitch.tv/ScufyKing/") == "scufyking");
    CHECK(NormalizeTwitchChannel("#Scufy_King") == "scufy_king");
    CHECK(!ParseIrcLine("", m));
    CHECK(!ParseIrcLine("@onlytags", m));
}

static void TestYouTube() {
    auto t = YouTube::ParseTarget("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=5", "");
    CHECK(t.videoId == "dQw4w9WgXcQ");
    CHECK(YouTube::ParseTarget("https://youtu.be/abcDEF12345", "").videoId == "abcDEF12345");
    CHECK(YouTube::ParseTarget("https://www.youtube.com/live/abcDEF12345?si=x", "").videoId == "abcDEF12345");
    CHECK(YouTube::ParseTarget("", "UCabcdefghijklmnopqrstuv").channelId == "UCabcdefghijklmnopqrstuv");
    CHECK(YouTube::ParseTarget("", "https://www.youtube.com/@ScufyKing").handle == "@ScufyKing");
    CHECK(YouTube::ParseTarget("", "@ScufyKing").handle == "@ScufyKing");
    CHECK(YouTube::MessagesPath("Cg0K==", "tok").find("liveChatId=Cg0K%3D%3D&pageToken=tok") != std::string::npos);
    CHECK(YouTube::MessagesPath("x", "").find("key=") == std::string::npos);   // key only in the header
    bool found = false, ended = false;
    CHECK(YouTube::ParseActiveLiveChatId(R"({"items":[{"id":"v","liveStreamingDetails":{"activeLiveChatId":"CHAT1"}}]})", &found, &ended) == "CHAT1" && found && !ended);
    CHECK(YouTube::ParseActiveLiveChatId(R"({"items":[]})", &found) == "" && !found);
    CHECK(YouTube::ParseLiveVideoId(R"({"items":[{"id":{"kind":"youtube#video","videoId":"VID"}}]})") == "VID");
    CHECK(YouTube::ParseChannelId(R"({"items":[{"id":"UC123"}]})") == "UC123");
    CHECK(YouTube::ParseErrorReason(R"({"error":{"code":403,"message":"quota","errors":[{"reason":"quotaExceeded"}]}})") == "quotaExceeded");
    const char* page = R"({"pollingIntervalMillis":5000,"nextPageToken":"N2","items":[
      {"id":"m1","snippet":{"type":"textMessageEvent","displayMessage":"Hallo","textMessageDetails":{"messageText":"Hallo"}},
       "authorDetails":{"channelId":"UCa","displayName":"Ana","isChatOwner":false,"isChatModerator":true}},
      {"id":"m2","snippet":{"type":"superChatEvent","displayMessage":"x","superChatDetails":{"amountDisplayString":"5,00 €","userComment":"Diesel!"}},
       "authorDetails":{"channelId":"UCb","displayName":"Bo"}},
      {"id":"m1","snippet":{"type":"tombstone"}},
      {"id":"m3","snippet":{"type":"userBannedEvent","userBannedDetails":{"bannedUserDetails":{"channelId":"UCx"}}}},
      {"id":"m4","snippet":{"type":"newSponsorEvent","displayMessage":"new member"}},
      {"id":"m5","snippet":{"type":"chatEndedEvent"}}]})";
    auto p = YouTube::ParseMessages(page, 1);
    CHECK(p.ok && p.pollingIntervalMs == 5000 && p.nextPageToken == "N2" && p.offline);
    CHECK(p.events.size() == 4);
    if (p.events.size() == 4) {
        CHECK(p.events[0].msg.user == "Ana" && p.events[0].msg.text == "Hallo" && p.events[0].msg.moderator);
        CHECK(p.events[1].msg.highlight && p.events[1].msg.text == "5,00 \xE2\x82\xAC \xC2\xB7 Diesel!");
        CHECK(p.events[2].kind == ChatEvent::Kind::DeleteMessage && p.events[2].key == "m1");
        CHECK(p.events[3].kind == ChatEvent::Kind::DeleteUser && p.events[3].key == "UCx");
    }
    CHECK(!YouTube::ParseMessages("not json", 1).ok);
    CHECK(YouTube::VideoIdFromText("  https://www.youtube.com/live/abcDEF12345?si=xyz \r\n") == "abcDEF12345");
    CHECK(YouTube::VideoIdFromText("https://studio.youtube.com/video/abcDEF12345/livestreaming") == "abcDEF12345");
    CHECK(YouTube::VideoIdFromText("Schaut rein: https://youtu.be/abcDEF12345 !") == "abcDEF12345");
    CHECK(YouTube::VideoIdFromText("https://m.youtube.com/watch?feature=share&v=abcDEF12345") == "abcDEF12345");
    CHECK(YouTube::VideoIdFromText("abcDEF12345") == "abcDEF12345");
    CHECK(YouTube::VideoIdFromText("hallo welt") == "");
    CHECK(YouTube::VideoIdFromText("https://www.youtube.com/@ScufyKing") == "");
}

static void TestLayout() {
    FakeGlyphs g;
    TextEngine te(g, U"?");
    Font f{20, false};
    auto L = te.Wrap(U"aaa bbb ccc ddd", f, 75, 0);   // 7 chars = 70 px per line
    CHECK(L.lines.size() == 2 && L.lines[0].text == U"aaa bbb" && L.lines[1].text == U"ccc ddd");
    L = te.Wrap(U"aaaaaaaaaaaaaaaaaaaa", f, 50, 0);   // long word broken by characters
    CHECK(L.lines.size() == 4 && L.lines[0].text.size() == 5);
    L = te.Wrap(U"aaa bbb ccc ddd eee fff", f, 75, 2);
    CHECK(L.truncated && L.lines.size() == 2 && L.lines[1].text.back() == U'…' && L.lines[1].width <= 75);
    L = te.Wrap(U"日本語日本語", f, 35, 0);   // CJK breaks between characters
    CHECK(L.lines.size() == 2);
    CHECK(te.Resolve(U"a\U0001F69Ab") == U"a?b");                      // no font has it -> replacement
    L = te.Wrap(U"xx yy", f, 75, 1, 40);                               // first-line indent
    CHECK(L.lines.size() == 1 && L.lines[0].indent == 40);
}

static void TestYouTubeWeb() {
    namespace W = YouTube::Web;
    const std::string watch =
        "<html><script>ytcfg.set({\"INNERTUBE_API_KEY\":\"AIzaWEBKEY\",\"INNERTUBE_CLIENT_VERSION\":\"2.20261010.00.00\","
        "\"INNERTUBE_CONTEXT\":{\"client\":{\"hl\":\"de\",\"clientName\":\"WEB\",\"clientVersion\":\"2.20261010.00.00\",\"visitorData\":\"Cgt2aXNpdG9y\"},"
        "\"user\":{\"lockedSafetyMode\":false}},\"INNERTUBE_CONTEXT_CLIENT_NAME\":1});</script>"
        "<script>var ytInitialData = {\"contents\":{\"twoColumnWatchNextResults\":{\"conversationBar\":{\"liveChatRenderer\":{"
        "\"continuations\":[{\"reloadContinuationData\":{\"continuation\":\"TOPCHAT\"}}],"
        "\"header\":{\"liveChatHeaderRenderer\":{\"viewSelector\":{\"sortFilterSubMenuRenderer\":{\"subMenuItems\":["
        "{\"title\":\"Top chat\",\"continuation\":{\"reloadContinuationData\":{\"continuation\":\"TOPCHAT\"}}},"
        "{\"title\":\"Live chat\",\"continuation\":{\"reloadContinuationData\":{\"continuation\":\"ALLCHAT\"}}}]}}}}}}}},"
        "\"note\":\"brace in string } and quote \\\" ok\"};</script></html>";
    auto wi = W::ParseWatchPage(watch);
    CHECK(wi.ok);
    CHECK(wi.apiKey == "AIzaWEBKEY");
    CHECK(wi.clientName == "1" && wi.clientVersion == "2.20261010.00.00" && wi.visitorData == "Cgt2aXNpdG9y");
    CHECK(wi.continuation == "ALLCHAT" && !wi.replay);
    CHECK(wi.candidates.size() == 2 && wi.candidates[1] == "TOPCHAT");
    CHECK(W::ParseYtcfg(watch).ok && W::ParseYtcfg(watch).candidates.empty());
    CHECK(YouTube::UrlEncodeKeepPercent("ab%3D/=") == "ab%3D%2F%3D");
    CHECK(W::PostBody(wi.contextJson, "C1").find("\"continuation\":\"C1\"") != std::string::npos);
    CHECK(!W::ParseWatchPage("<html>consent.youtube.com</html>").ok);
    CHECK(W::VideoIdFromLivePage("<link rel=\"canonical\" href=\"https://www.youtube.com/watch?v=abcDEF12345\">") == "abcDEF12345");
    CHECK(W::VideoIdFromLivePage("<link rel=\"canonical\" href=\"https://www.youtube.com/@ScufyKing\">") == "");
    YouTube::Target t; t.handle = "@ScufyKing";
    CHECK(W::LivePagePath(t) == "/@ScufyKing/live");
    t.channelId = "UCabc"; CHECK(W::LivePagePath(t) == "/channel/UCabc/live");
    const std::string poll = R"({"continuationContents":{"liveChatContinuation":{
      "continuations":[{"invalidationContinuationData":{"continuation":"NEXT","timeoutMs":5000}}],
      "actions":[
        {"addChatItemAction":{"item":{"liveChatTextMessageRenderer":{"id":"m1","authorExternalChannelId":"UCa",
          "authorName":{"simpleText":"Ana"},"message":{"runs":[{"text":"Hallo "},{"emoji":{"emojiId":"🚚","shortcuts":[":truck:"]}},
          {"text":" "},{"emoji":{"emojiId":"UCx/abc","shortcuts":[":_hupe:"],"isCustomEmoji":true}}]},
          "authorBadges":[{"liveChatAuthorBadgeRenderer":{"icon":{"iconType":"MODERATOR"}}}]}}}},
        {"addChatItemAction":{"item":{"liveChatPaidMessageRenderer":{"id":"m2","authorName":{"simpleText":"Bo"},
          "purchaseAmountText":{"simpleText":"5,00 €"},"message":{"runs":[{"text":"Diesel!"}]}}}}},
        {"addChatItemAction":{"item":{"liveChatMembershipItemRenderer":{"id":"m3"}}}},
        {"markChatItemAsDeletedAction":{"targetItemId":"m1"}},
        {"markChatItemsByAuthorAsDeletedAction":{"externalChannelId":"UCx"}}]}}})";
    auto cp = W::ParseChat(poll, 1);
    CHECK(cp.ok && cp.continuation == "NEXT" && cp.timeoutMs == 5000);
    CHECK(cp.events.size() == 4);
    if (cp.events.size() == 4) {
        CHECK(cp.events[0].msg.user == "Ana" && cp.events[0].msg.text == "Hallo \xF0\x9F\x9A\x9A :_hupe:" && cp.events[0].msg.moderator);
        CHECK(cp.events[1].msg.highlight && cp.events[1].msg.text == "5,00 \xE2\x82\xAC \xC2\xB7 Diesel!");
        CHECK(cp.events[2].kind == ChatEvent::Kind::DeleteMessage && cp.events[2].key == "m1");
        CHECK(cp.events[3].kind == ChatEvent::Kind::DeleteUser && cp.events[3].key == "UCx");
    }
    CHECK(!W::ParseChat("{}", 1).ok);
    CHECK(W::ParseChat(R"({"continuationContents":{"liveChatContinuation":{"actions":[]}}})", 1).continuation.empty());
    std::string page = "<script>window[\"ytInitialData\"] = " + std::string(poll) + ";</script>";
    CHECK(W::ParseChat(W::ExtractInitialData(page), 1).continuation == "NEXT");
    // hex-escaped variant: var ytInitialData = '\x7b\x22a\x22:1\x7d';
    CHECK(W::ExtractInitialData("<script>var ytInitialData = '\\x7b\\x22a\\x22:\\x22\\u00e4\\x22\\x7d';</script>") == "{\"a\":\"\xC3\xA4\"}");
}

int main() {
    TestUtf8(); TestSanitize(); TestFilter(); TestTwitch(); TestYouTube(); TestYouTubeWeb(); TestLayout();
    printf("%d checks passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
