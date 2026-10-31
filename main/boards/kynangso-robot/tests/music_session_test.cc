#include "robot_music_session.h"

#include <cassert>
#include <cstdio>
#include <string>

using kynangso::RobotMusicSession;
using Update = RobotMusicSession::Update;

int main() {
    RobotMusicSession session;
    assert(session.OnAssistantText("Xin chào") == Update::None);
    assert(session.OnAssistantText("% search_music...") == Update::None);
    assert(session.OnAssistantText("play_music") == Update::None);
    assert(session.OnAssistantText("Tôi gọi play_music...") == Update::None);
    assert(session.OnAssistantText("% play_music_fake...") == Update::None);
    assert(session.OnAssistantText("% play_music is not a tool marker") == Update::None);
    assert(!session.IsActive());

    // Replay the user's tenclass sentence_start sequence.
    assert(session.OnAssistantText("% play_music...") == Update::Started);
    assert(session.IsActive());
    assert(session.OnAssistantText("《远方》") == Update::Text);
    assert(session.Title() == "《远方》");
    assert(session.CurrentLine().empty());
    assert(session.OnAssistantText("晨风散迷茫，行囊向远方。") == Update::Text);
    assert(session.OnAssistantText("心跳伴足音，裙角吻晨光。") == Update::Text);
    assert(session.PreviousLine() == "晨风散迷茫，行囊向远方。");
    assert(session.CurrentLine() == "心跳伴足音，裙角吻晨光。");
    session.OnAssistantText("心跳伴足音，裙角吻晨光。");
    assert(session.PreviousLine() == "晨风散迷茫，行囊向远方。");
    assert(session.OnAssistantText("% self.robot.show_payment_qr...") == Update::None);
    assert(session.CurrentLine() == "心跳伴足音，裙角吻晨光。");
    session.OnAssistantText("% play_music...");
    assert(session.Title().empty() && session.CurrentLine().empty() &&
           session.PreviousLine().empty());
    session.OnAssistantText("Đường về nhà, tiếng Việt có dấu");
    assert(session.CurrentLine() == "Đường về nhà, tiếng Việt có dấu");

    std::string huge;
    for (int i = 0; i < 2000; ++i)
        huge += "ế";
    session.OnAssistantText(huge);
    assert(session.CurrentLine().size() <= 384);
    assert(session.CurrentLine().size() % 3 == 0);
    session.OnAssistantText("《" + huge + "》");
    assert(session.Title().size() <= 192);
    assert(session.Title().size() % 3 == 0);

    assert(session.OnAssistantText(" % pause_music... \n") == Update::Stopped);
    assert(!session.IsActive() && session.CurrentLine().empty());
    session.OnAssistantText("% play_music…");
    assert(session.IsActive());
    assert(session.OnAssistantText("% stop_music...") == Update::Stopped);
    session.OnAssistantText("% play_music");
    session.Reset();
    assert(!session.IsActive() && session.Title().empty() && session.PreviousLine().empty());
    assert(session.OnAssistantText("TTS bình thường sau bài hát") == Update::None);
    std::puts(
        "PASS tenclass music markers, title/lyrics, UTF-8 bounds, stop/reset and ordinary speech");
}
