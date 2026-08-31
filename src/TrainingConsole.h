/*
 * TrainingConsole.h
 * ---------------------------------------------------------------------------
 * The serial-monitor training system.
 *
 * Commands, typed straight into the Arduino Serial Monitor:
 *
 *   train | question | answer      teach a new question and answer
 *   train | delete | 0001          delete one entry by its data number
 *   train | delete | full data     delete every taught entry
 *   train | list                   show everything that has been taught
 *   train | status                 memory and capacity report
 *   train | save                   force a write to flash
 *   train | help                   command reminder
 *
 * A successful lesson answers exactly:
 *   train successfully and save data number code 0001
 *
 * Training stops while free heap is at or below the reserved minimum (200 KB
 * by default). That reserve keeps chat logging and the matcher working, so a
 * full memory can never take the chatbot down; the console says so plainly
 * instead of failing silently.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_TRAINING_CONSOLE_H
#define AMELTECH_TRAINING_CONSOLE_H

#include <Arduino.h>
#include "AmelTechConfig.h"
#include "KnowledgeBase.h"

class TrainingConsole {
public:
    TrainingConsole();

    void begin(KnowledgeBase* kb);

    // True when the line starts with the training keyword.
    static bool isTrainingCommand(const char* line);

    // Runs the command and returns the message to print. Only call it when
    // isTrainingCommand() is true.
    String handle(const String& line);

    // Convenience wrapper used by the bot's train(question, answer) API.
    String teach(const String& question, const String& answer,
                 const char* category = "user");

    uint32_t lessonsAccepted() const { return _accepted; }
    uint32_t lessonsRejected() const { return _rejected; }
    uint16_t lastCode() const { return _lastCode; }

    // Free heap that must remain after a lesson is stored.
    void setMinFreeHeap(uint32_t bytes);
    uint32_t minFreeHeap() const { return _minFreeHeap; }

    static uint32_t freeHeapBytes();
    String statusReport() const;
    static String helpText();

private:
    KnowledgeBase* _kb;
    uint32_t _accepted;
    uint32_t _rejected;
    uint16_t _lastCode;
    uint32_t _minFreeHeap;

    String deleteCommand(const String& target);
    String describeAddError(int8_t code, const String& question) const;
};

#endif // AMELTECH_TRAINING_CONSOLE_H
