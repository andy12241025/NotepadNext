/*
 * This file is part of Notepad Next.
 * Copyright 2024 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "TranslationManager.h"

#include <QApplication>
#include <QDirIterator>
#include <QDebug>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

QString TranslationFileNameToLocaleName(const QString &baseName)
{
    // return "zh_CN" from "NotepadNext_zh_CN"
    return baseName.mid(QApplication::applicationName().length() + 1); // +1 for the underscore
}


TranslationManager::TranslationManager(QObject *parent, const QString &path)
    : QObject(parent), path(path)
{
}

QStringList TranslationManager::availableTranslations() const
{
    QStringList translations;
    QDirIterator it(path);

    while (it.hasNext()) {
        it.next();

        if (it.fileName().startsWith(QApplication::applicationName())) { // Some are Qt translation files, not for the application
            QString localeName = TranslationFileNameToLocaleName(it.fileInfo().baseName());

            translations.append(localeName);
        }
    }

    return translations;
}

QString TranslationManager::FormatLocaleTerritoryAndLanguage(QLocale &locale)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 2, 0)
    const QString territory = QLocale::countryToString(locale.country());
#else
    const QString territory = QLocale::territoryToString(locale.territory());
#endif
    const QString language = QLocale::languageToString(locale.language());
    return QStringLiteral("%1 (%2)").arg(language, territory);
}

QString TranslationManager::systemUiLanguageName()
{
#ifdef Q_OS_WIN
    // This is the Windows display language. QLocale::system() is the regional format, and its list of
    // UI languages can still end up naming a different language than the one Windows is displayed in.
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
        return QString::fromWCharArray(name);
    }
#endif

    const QStringList uiLanguages = QLocale::system().uiLanguages();
    return uiLanguages.isEmpty() ? QLocale::system().name() : uiLanguages.first();
}

void TranslationManager::loadSystemDefaultTranslation()
{
    // Don't use QLocale::system().name(), that is the regional format (e.g. "zh_CN" when the format
    // is Chinese but the display language is English).
    // The locale is built from a name since passing QLocale::system() directly can load the wrong file.
    const QLocale locale(systemUiLanguageName());

    // English is the source language and has no translation file. Loading would otherwise fall
    // through to the next preferred language and translate the UI anyway.
    if (locale.language() == QLocale::English) {
        qInfo("Using built-in English");
        return;
    }

    loadTranslation(locale);
}

void TranslationManager::loadTranslation(QLocale locale)
{
    qInfo(Q_FUNC_INFO);

    const QStringList filenames{QStringList{QApplication::applicationName(), "qt", "qtbase"}};

    for (const auto& filename : filenames) {
        std::unique_ptr<QTranslator> translator = std::make_unique<QTranslator>();

        if (translator->load(locale, filename, QStringLiteral("_"), path)) {
            if (QCoreApplication::installTranslator(translator.get())) {
                qInfo() << "Loaded translation" << translator.get()->filePath();
                translators.append(translator.release());
            }
        }
    }
}

void TranslationManager::loadTranslation(QString localeName)
{
    loadTranslation(QLocale(localeName));
}
