#ifndef LOG_H
#define LOG_H

#include <QDebug>

// ============================================================
// SISTEMA DE LOGGING MULTINIVEL
// ============================================================
// Niveles (de menor a mayor verbosidad):
//   0 = OFF      - Sin logs
//   1 = ERROR    - Solo errores críticos
//   2 = WARNING  - Errores + advertencias
//   3 = INFO     - + información general
//   4 = DEBUG    - + mensajes de debug
//   5 = VERBOSE  - + detalles extra
//   6 = TRACE    - + entrada/salida de funciones
// ============================================================

// Nivel por defecto según tipo de build
#ifdef QT_DEBUG
    #define LOG_LEVEL_DEFAULT 6  // Debug: todos los logs
#else
    #define LOG_LEVEL_DEFAULT 3  // Release: solo INFO y superiores
#endif

// Permite override desde CMake: -DLOG_LEVEL=4
#ifndef LOG_LEVEL
    #define LOG_LEVEL LOG_LEVEL_DEFAULT
#endif

// ============================================================
// MACROS DE LOGGING
// ============================================================

#if LOG_LEVEL >= 1
    #define LOG_ERROR(msg)   qCritical() << "[ERROR]" << msg
#else
    #define LOG_ERROR(msg)   ((void)0)
#endif

#if LOG_LEVEL >= 2
    #define LOG_WARNING(msg) qWarning() << "[WARN]" << msg
#else
    #define LOG_WARNING(msg) ((void)0)
#endif

#if LOG_LEVEL >= 3
    #define LOG_INFO(msg)    qInfo() << "[INFO]" << msg
#else
    #define LOG_INFO(msg)    ((void)0)
#endif

#if LOG_LEVEL >= 4
    #define LOG_DEBUG(msg)   qDebug() << "[DEBUG]" << msg
#else
    #define LOG_DEBUG(msg)   ((void)0)
#endif

#if LOG_LEVEL >= 5
    #define LOG_VERBOSE(msg) qDebug() << "[VERBOSE]" << msg
#else
    #define LOG_VERBOSE(msg) ((void)0)
#endif

#if LOG_LEVEL >= 6
    #define LOG_TRACE(msg)   qDebug() << "[TRACE]" << __FUNCTION__ << "->" << msg
#else
    #define LOG_TRACE(msg)   ((void)0)
#endif

#endif // LOG_H
