// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qcoloroutput_p.h"

#include <QtCore/qfile.h>
#include <QtCore/qhash.h>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

QT_BEGIN_NAMESPACE

class QColorOutputPrivate
{
public:
    QColorOutputPrivate()
    {
        m_coloringEnabled = isColoringPossible();
    }

    static const char *const foregrounds[];
    static const char *const backgrounds[];

    inline void write(const QString &msg)
    {
        m_buffer.append(msg.toLocal8Bit());
    }

    static QString escapeCode(const QString &in)
    {
        const ushort escapeChar = 0x1B;
        QString result;
        result.append(QChar(escapeChar));
        result.append(QLatin1Char('['));
        result.append(in);
        result.append(QLatin1Char('m'));
        return result;
    }

    void insertColor(int id, QColorOutput::ColorCode code) { m_colorMapping.insert(id, code); }
    QColorOutput::ColorCode color(int id) const { return m_colorMapping.value(id); }
    bool containsColor(int id) const { return m_colorMapping.contains(id); }

    void setSilent(bool silent) { m_silent = silent; }
    bool isSilent() const { return m_silent; }

    void setCurrentColorID(int colorId) { m_currentColorID = colorId; }

    bool coloringEnabled() const { return m_coloringEnabled; }

    void flushBuffer()
    {
        fwrite(m_buffer.constData(), size_t(1), size_t(m_buffer.size()), stderr);
        m_buffer.clear();
    }

    void discardBuffer()
    {
        m_buffer.clear();
    }

private:
    QByteArray                  m_buffer;
    QColorOutput::ColorMapping  m_colorMapping;
    int                         m_currentColorID = -1;
    bool                        m_coloringEnabled = false;
    bool                        m_silent = false;

    /*
     Returns true if it's suitable to send colored output to \c stderr.
     */
    inline bool isColoringPossible() const
    {
#if defined(Q_OS_WIN)
        /* Windows doesn't at all support ANSI escape codes, unless
         * the user install a "device driver". See the Wikipedia links in the
         * class documentation for details. */
        return false;
#else
        /* We use QFile::handle() to get the file descriptor. It's a bit unsure
         * whether it's 2 on all platforms and in all cases, so hopefully this layer
         * of abstraction helps handle such cases. */
        return isatty(fileno(stderr));
#endif
    }
};

const char *const QColorOutputPrivate::foregrounds[] =
{
    "0;30",
    "0;34",
    "0;32",
    "0;36",
    "0;31",
    "0;35",
    "0;33",
    "0;37",
    "1;30",
    "1;34",
    "1;32",
    "1;36",
    "1;31",
    "1;35",
    "1;33",
    "1;37"
};

const char *const QColorOutputPrivate::backgrounds[] =
{
    "0;40",
    "0;44",
    "0;42",
    "0;46",
    "0;41",
    "0;45",
    "0;43"
};

/*!
  \class QColorOutput
  \nonreentrant
  \brief Outputs colored messages to \c stderr.
  \internal

  QColorOutput is a convenience class for outputting messages to \c
  stderr using color escape codes, as mandated in ECMA-48. QColorOutput
  will only color output when it is detected to be suitable. For
  instance, if \c stderr is detected to be attached to a file instead
  of a TTY, no coloring will be done.

  QColorOutput does its best attempt. but it is generally undefined
  what coloring or effect the various coloring flags has. It depends
  strongly on what terminal software that is being used.

  When using `echo -e 'my escape sequence'`, \c{\033} works as an
  initiator but not when printing from a C++ program, despite having
  escaped the backslash.  That's why we below use characters with
  value 0x1B.

  It can be convenient to subclass QColorOutput with a private scope,
  such that the functions are directly available in the class using
  it.

  \section1 Usage

  To output messages, call write() or writeUncolored(). write() takes
  as second argument an integer, which QColorOutput uses as a lookup
  key to find the color it should color the text in. The mapping from
  keys to colors is done using insertMapping(). Typically this is used
  by having enums for the various kinds of messages, which
  subsequently are registered.

  \code
  enum MyMessage
  {
    Error,
    Important
  };

  QColorOutput output;
  output.insertMapping(Error, QColorOutput::RedForeground);
  output.insertMapping(Import, QColorOutput::BlueForeground);

  output.write("This is important", Important);
  output.write("Jack, I'm only the selected official!", Error);
  \endcode

  \sa {http://tldp.org/HOWTO/Bash-Prompt-HOWTO/x329.html}{Bash Prompt HOWTO, 6.1. Colors},
      {http://linuxgazette.net/issue51/livingston-blade.html}{Linux Gazette, Tweaking Eterm, Edward Livingston-Blade},
      {http://www.ecma-international.org/publications/standards/Ecma-048.htm}{Standard ECMA-48, Control Functions for Coded Character Sets, ECMA International},
      {http://en.wikipedia.org/wiki/ANSI_escape_code}{Wikipedia, ANSI escape code},
      {http://linuxgazette.net/issue65/padala.html}{Linux Gazette, So You Like Color!, Pradeep Padala}
 */

/*!
  \internal
  \enum QColorOutput::ColorCodeComponent
  \value BlackForeground
  \value BlueForeground
  \value GreenForeground
  \value CyanForeground
  \value RedForeground
  \value PurpleForeground
  \value BrownForeground
  \value LightGrayForeground
  \value DarkGrayForeground
  \value LightBlueForeground
  \value LightGreenForeground
  \value LightCyanForeground
  \value LightRedForeground
  \value LightPurpleForeground
  \value YellowForeground
  \value WhiteForeground
  \value BlackBackground
  \value BlueBackground
  \value GreenBackground
  \value CyanBackground
  \value RedBackground
  \value PurpleBackground
  \value BrownBackground

  \value DefaultColor QColorOutput performs no coloring. This typically
                     means black on white or white on black, depending
                     on the settings of the user's terminal.
 */

/*!
  \internal
  Constructs a QColorOutput instance, ready for use.
 */
QColorOutput::QColorOutput() : d(new QColorOutputPrivate) {}

// must be here so that QScopedPointer has access to the complete type
QColorOutput::~QColorOutput() = default;

bool QColorOutput::isSilent() const { return d->isSilent(); }
void QColorOutput::setSilent(bool silent) { d->setSilent(silent); }

/*!
 \internal
 Sends \a message to \c stderr, using the color looked up in the color mapping using \a colorID.

 If \a color isn't available in the color mapping, result and behavior is undefined.

 If \a colorID is 0, which is the default value, the previously used coloring is used. QColorOutput
 is initialized to not color at all.

 If \a message is empty, effects are undefined.

 \a message will be printed as is. For instance, no line endings will be inserted.
 */
void QColorOutput::write(QStringView message, int colorID)
{
    if (!d->isSilent())
        d->write(colorify(message, colorID));
}

void QColorOutput::writePrefixedMessage(const QString &message, QtMsgType type,
                                       const QString &prefix)
{
    static const QHash<QtMsgType, QString> prefixes = {
        {QtMsgType::QtCriticalMsg, QStringLiteral("Error")},
        {QtMsgType::QtWarningMsg, QStringLiteral("Warning")},
        {QtMsgType::QtInfoMsg, QStringLiteral("Info")},
        {QtMsgType::QtDebugMsg, QStringLiteral("Hint")}
    };

    Q_ASSERT(prefixes.contains(type));
    Q_ASSERT(prefix.isEmpty() || prefix.front().isUpper());
    write((prefix.isEmpty() ? prefixes[type] : prefix) + QStringLiteral(": "), type);
    writeUncolored(message);
}

/*!
 \internal
 Writes \a message to \c stderr as if for instance
 QTextStream would have been used, and adds a line ending at the end.

 This function can be practical to use such that one can use QColorOutput for all forms of writing.
 */
void QColorOutput::writeUncolored(const QString &message)
{
    if (!d->isSilent())
        d->write(message + QLatin1Char('\n'));
}

/*!
 \internal
 Treats \a message and \a colorID identically to write(), but instead of writing
 \a message to \c stderr, it is prepared for being written to \c stderr, but is then
 returned.

 This is useful when the colored string is inserted into a translated string(dividing
 the string into several small strings prevents proper translation).
 */
QString QColorOutput::colorify(const QStringView message, int colorID) const
{
    Q_ASSERT_X(colorID == -1 || d->containsColor(colorID), Q_FUNC_INFO,
               qPrintable(QString::fromLatin1("There is no color registered by id %1")
                          .arg(colorID)));
    Q_ASSERT_X(!message.isEmpty(), Q_FUNC_INFO,
               "It makes no sense to attempt to print an empty string.");

    if (colorID != -1)
        d->setCurrentColorID(colorID);

    if (d->coloringEnabled() && colorID != -1) {
        const int color = d->color(colorID);

        /* If DefaultColor is set, we don't want to color it. */
        if (color & DefaultColor)
            return message.toString();

        const int foregroundCode = (color & ForegroundMask) >> ForegroundShift;
        const int backgroundCode = (color & BackgroundMask) >> BackgroundShift;
        QString finalMessage;
        bool closureNeeded = false;

        if (foregroundCode > 0) {
            finalMessage.append(
                        QColorOutputPrivate::escapeCode(
                            QLatin1String(QColorOutputPrivate::foregrounds[foregroundCode - 1])));
            closureNeeded = true;
        }

        if (backgroundCode > 0) {
            finalMessage.append(
                        QColorOutputPrivate::escapeCode(
                            QLatin1String(QColorOutputPrivate::backgrounds[backgroundCode - 1])));
            closureNeeded = true;
        }

        finalMessage.append(message);

        if (closureNeeded)
            finalMessage.append(QColorOutputPrivate::escapeCode(QLatin1String("0")));

        return finalMessage;
    }

    return message.toString();
}

void QColorOutput::flushBuffer()
{
    d->flushBuffer();
}

void QColorOutput::discardBuffer()
{
    d->discardBuffer();
}

/*!
  \internal
  Adds a color mapping from \a colorID to \a colorCode, for this QColorOutput instance.
 */
void QColorOutput::insertMapping(int colorID, const ColorCode colorCode)
{
    d->insertColor(colorID, colorCode);
}

QT_END_NAMESPACE
