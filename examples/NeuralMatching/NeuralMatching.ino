/*
 * NeuralMatching
 * ---------------------------------------------------------------------------
 * A look inside the matcher.
 *
 * The engine is described as "neural style" because it embeds text into a
 * fixed 48 dimensional vector and blends several similarity measures, which
 * lets it generalise over wording and survive typos. It is NOT a trained
 * network: the projection is deterministic feature hashing, the weights are
 * fixed constants, and the same question always produces the same answer. It
 * cannot invent a fact, which is exactly what you want on a microcontroller.
 *
 * Scoring blend:
 *   0.34 token overlap   0.24 embedding cosine   0.18 trigram Dice
 *   0.16 edit similarity 0.08 token signature
 * plus small bonuses for containment and question-word agreement, then a
 * calibration curve that maps the raw score onto a confidence band.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

void showRanking(const char* question) {
    Serial.print("Question: ");
    Serial.println(question);

    AmelTechQuery q;
    NeuralEngine::buildQuery(question, q);

    Serial.print("  normalized : ");
    Serial.println(q.normalized);
    Serial.print("  tokens     : ");
    for (int i = 0; i < q.tokenCount; ++i) {
        Serial.print(q.tokens[i]);
        Serial.print(' ');
    }
    Serial.println();

    MatchResult top[3];
    uint8_t n = bot.knowledge().rank(q, top, 3);

    for (uint8_t i = 0; i < n; ++i) {
        if (!top[i].found) continue;
        Serial.print("  #");
        Serial.print(i + 1);
        Serial.print("  conf ");
        Serial.print(top[i].confidence, 3);
        Serial.print("  ");
        Serial.println(top[i].matchedQuestion);
    }

    Serial.print("  scan ");
    Serial.print(bot.knowledge().lastScanMicros());
    Serial.print(" us over ");
    Serial.print((unsigned long)bot.knowledge().totalCount());
    Serial.print(" entries, ");
    Serial.print(bot.knowledge().lastCandidateCount());
    Serial.println(" candidates fully scored");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    Serial.println();
    Serial.println("=== How the matcher sees your question ===");
    Serial.println();

    showRanking("what is wifi");
    showRanking("whats wi-fi");
    showRanking("what is teh speed of light");
    showRanking("tell me about bluetooth");
    showRanking("how many sec r there in 1 min");
    showRanking("completely unrelated nonsense");

    // Text utilities are public, which makes debugging your own data easy.
    char buf[128];
    AmelTechText::normalize("What's an ESP-32's GPIO?", buf, sizeof(buf));
    Serial.print("normalize(\"What's an ESP-32's GPIO?\") = ");
    Serial.println(buf);

    Serial.print("editSimilarity(\"gravity\", \"gravty\") = ");
    Serial.println(AmelTechText::editSimilarity("gravity", "gravty"), 3);
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
    }
    bot.tick();
}
