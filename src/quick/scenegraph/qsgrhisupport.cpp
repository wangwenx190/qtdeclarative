/****************************************************************************
**
** Copyright (C) 2019 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQuick module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsgrhisupport_p.h"
#include "qsgcontext_p.h"
#include "qsgdefaultrendercontext_p.h"

#include <QtQuick/private/qquickitem_p.h>
#include <QtQuick/private/qquickwindow_p.h>

#include <QtGui/qwindow.h>

#if QT_CONFIG(vulkan)
#include <QtGui/private/qvulkandefaultinstance_p.h>
#endif

#include <QOperatingSystemVersion>
#include <QOffscreenSurface>

#ifdef Q_OS_WIN
#include <dxgiformat.h>
#endif

#include <memory>

QT_BEGIN_NAMESPACE

QSGRhiSupport::QSGRhiSupport()
    : m_settingsApplied(false),
      m_debugLayer(false),
      m_profile(false),
      m_shaderEffectDebug(false),
      m_preferSoftwareRenderer(false)
{
}

void QSGRhiSupport::applySettings()
{
    // Multiple calls to this function are perfectly possible!
    // Just store that it was called at least once.
    m_settingsApplied = true;

    // This is also done when creating the renderloop but we may be before that
    // in case we get here due to a setGraphicsApi() -> configure() early
    // on in main(). Avoid losing info logs since troubleshooting gets
    // confusing otherwise.
    QSGRhiSupport::checkEnvQSgInfo();

    if (m_requested.valid) {
        // explicit rhi backend request from C++ (e.g. via QQuickWindow)
        switch (m_requested.api) {
        case QSGRendererInterface::OpenGLRhi:
            m_rhiBackend = QRhi::OpenGLES2;
            break;
        case QSGRendererInterface::Direct3D11Rhi:
            m_rhiBackend = QRhi::D3D11;
            break;
        case QSGRendererInterface::VulkanRhi:
            m_rhiBackend = QRhi::Vulkan;
            break;
        case QSGRendererInterface::MetalRhi:
            m_rhiBackend = QRhi::Metal;
            break;
        case QSGRendererInterface::NullRhi:
            m_rhiBackend = QRhi::Null;
            break;
        default:
            Q_ASSERT_X(false, "QSGRhiSupport", "Internal error: unhandled GraphicsApi type");
            break;
        }
    } else {
        // check env.vars., fall back to platform-specific defaults when backend is not set
        const QByteArray rhiBackend = qgetenv("QSG_RHI_BACKEND");
        if (rhiBackend == QByteArrayLiteral("gl")
                || rhiBackend == QByteArrayLiteral("gles2")
                || rhiBackend == QByteArrayLiteral("opengl"))
        {
            m_rhiBackend = QRhi::OpenGLES2;
        } else if (rhiBackend == QByteArrayLiteral("d3d11") || rhiBackend == QByteArrayLiteral("d3d")) {
            m_rhiBackend = QRhi::D3D11;
        } else if (rhiBackend == QByteArrayLiteral("vulkan")) {
            m_rhiBackend = QRhi::Vulkan;
        } else if (rhiBackend == QByteArrayLiteral("metal")) {
            m_rhiBackend = QRhi::Metal;
        } else if (rhiBackend == QByteArrayLiteral("null")) {
            m_rhiBackend = QRhi::Null;
        } else {
            if (!rhiBackend.isEmpty()) {
                qWarning("Unknown key \"%s\" for QSG_RHI_BACKEND, falling back to default backend.",
                         rhiBackend.constData());
            }
#if defined(Q_OS_WIN)
            m_rhiBackend = QRhi::D3D11;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
            m_rhiBackend = QRhi::Metal;
#elif QT_CONFIG(opengl)
            m_rhiBackend = QRhi::OpenGLES2;
#else
            m_rhiBackend = QRhi::Vulkan;
#endif

            // Now that we established our initial choice, we may want to opt
            // for another backend under certain special circumstances.
            adjustToPlatformQuirks();
        }
    }

    // At this point the RHI backend is fixed, it cannot be changed once we
    // return from this function. This is because things like the QWindow
    // (QQuickWindow) may depend on the graphics API as well (surfaceType
    // f.ex.), and all that is based on what we report from here. So further
    // adjustments are not possible (or, at minimum, not safe and portable).

    // validation layers (Vulkan) or debug layer (D3D)
    m_debugLayer = uint(qEnvironmentVariableIntValue("QSG_RHI_DEBUG_LAYER"));

    // EnableProfiling + DebugMarkers
    m_profile = uint(qEnvironmentVariableIntValue("QSG_RHI_PROFILE"));

    // EnablePipelineCacheDataSave
    m_pipelineCacheSave = qEnvironmentVariable("QSG_RHI_PIPELINE_CACHE_SAVE");

    m_pipelineCacheLoad = qEnvironmentVariable("QSG_RHI_PIPELINE_CACHE_LOAD");

    m_shaderEffectDebug = uint(qEnvironmentVariableIntValue("QSG_RHI_SHADEREFFECT_DEBUG"));

    m_preferSoftwareRenderer = uint(qEnvironmentVariableIntValue("QSG_RHI_PREFER_SOFTWARE_RENDERER"));

    m_killDeviceFrameCount = qEnvironmentVariableIntValue("QSG_RHI_SIMULATE_DEVICE_LOSS");
    if (m_killDeviceFrameCount > 0 && m_rhiBackend == QRhi::D3D11)
        qDebug("Graphics device will be reset every %d frames", m_killDeviceFrameCount);

    QByteArray hdrRequest = qgetenv("QSG_RHI_HDR");
    if (!hdrRequest.isEmpty()) {
        hdrRequest = hdrRequest.toLower();
        if (hdrRequest == QByteArrayLiteral("scrgb") || hdrRequest == QByteArrayLiteral("extendedsrgblinear"))
            m_swapChainFormat = QRhiSwapChain::HDRExtendedSrgbLinear;
        else if (hdrRequest == QByteArrayLiteral("hdr10"))
            m_swapChainFormat = QRhiSwapChain::HDR10;
        else
            qWarning("Unknown HDR mode '%s'", hdrRequest.constData());
    }

    const QString backendName = rhiBackendName();
    qCDebug(QSG_LOG_INFO,
            "Using QRhi with backend %s\n"
            "  Graphics API debug/validation layers: %d\n"
            "  QRhi profiling and debug markers: %d\n"
            "  Shader/pipeline cache collection: %d",
            qPrintable(backendName), m_debugLayer, m_profile, !m_pipelineCacheSave.isEmpty());
    if (m_preferSoftwareRenderer)
        qCDebug(QSG_LOG_INFO, "Prioritizing software renderers");
}

void QSGRhiSupport::adjustToPlatformQuirks()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    // A macOS VM may not have Metal support at all. We have to decide at this
    // point, it will be too late afterwards, and the only way is to see if
    // MTLCreateSystemDefaultDevice succeeds.
    if (m_rhiBackend == QRhi::Metal) {
        QRhiMetalInitParams rhiParams;
        if (!QRhi::probe(m_rhiBackend, &rhiParams)) {
            m_rhiBackend = QRhi::OpenGLES2;
            qCDebug(QSG_LOG_INFO, "Metal does not seem to be supported. Falling back to OpenGL.");
        }
    }
#endif
}

void QSGRhiSupport::checkEnvQSgInfo()
{
    // For compatibility with 5.3 and earlier's QSG_INFO environment variables
    if (qEnvironmentVariableIsSet("QSG_INFO"))
        const_cast<QLoggingCategory &>(QSG_LOG_INFO()).setEnabled(QtDebugMsg, true);
}


#if QT_CONFIG(opengl)
#ifndef GL_BGRA
#define GL_BGRA                           0x80E1
#endif

#ifndef GL_R8
#define GL_R8                             0x8229
#endif

#ifndef GL_RG8
#define GL_RG8                            0x822B
#endif

#ifndef GL_RG
#define GL_RG                             0x8227
#endif

#ifndef GL_R16
#define GL_R16                            0x822A
#endif

#ifndef GL_RG16
#define GL_RG16                           0x822C
#endif

#ifndef GL_RED
#define GL_RED                            0x1903
#endif

#ifndef GL_RGBA8
#define GL_RGBA8                          0x8058
#endif

#ifndef GL_RGBA32F
#define GL_RGBA32F                        0x8814
#endif

#ifndef GL_RGBA16F
#define GL_RGBA16F                        0x881A
#endif

#ifndef GL_R16F
#define GL_R16F                           0x822D
#endif

#ifndef GL_R32F
#define GL_R32F                           0x822E
#endif

#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT                     0x140B
#endif

#ifndef GL_DEPTH_COMPONENT16
#define GL_DEPTH_COMPONENT16              0x81A5
#endif

#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24              0x81A6
#endif

#ifndef GL_DEPTH_COMPONENT32F
#define GL_DEPTH_COMPONENT32F             0x8CAC
#endif

#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX                  0x1901
#endif

#ifndef GL_STENCIL_INDEX8
#define GL_STENCIL_INDEX8                 0x8D48
#endif

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8               0x88F0
#endif

#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#endif

#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL                  0x84F9
#endif

#ifndef GL_PRIMITIVE_RESTART_FIXED_INDEX
#define GL_PRIMITIVE_RESTART_FIXED_INDEX  0x8D69
#endif

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER               0x8CA8
#endif

#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#endif

#ifndef GL_MAX_DRAW_BUFFERS
#define GL_MAX_DRAW_BUFFERS               0x8824
#endif

#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE           0x884C
#endif

#ifndef GL_COMPARE_REF_TO_TEXTURE
#define GL_COMPARE_REF_TO_TEXTURE         0x884E
#endif

#ifndef GL_TEXTURE_COMPARE_FUNC
#define GL_TEXTURE_COMPARE_FUNC           0x884D
#endif

#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES                    0x8D57
#endif

#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER          0x90D2
#endif

#ifndef GL_READ_ONLY
#define GL_READ_ONLY                      0x88B8
#endif

#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY                     0x88B9
#endif

#ifndef GL_READ_WRITE
#define GL_READ_WRITE                     0x88BA
#endif

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER                 0x91B9
#endif

#ifndef GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#endif

#ifndef GL_ELEMENT_ARRAY_BARRIER_BIT
#define GL_ELEMENT_ARRAY_BARRIER_BIT       0x00000002
#endif

#ifndef GL_UNIFORM_BARRIER_BIT
#define GL_UNIFORM_BARRIER_BIT             0x00000004
#endif

#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT       0x00000200
#endif

#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT      0x00002000
#endif

#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT       0x00000008
#endif

#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif

#ifndef GL_PIXEL_BUFFER_BARRIER_BIT
#define GL_PIXEL_BUFFER_BARRIER_BIT        0x00000080
#endif

#ifndef GL_TEXTURE_UPDATE_BARRIER_BIT
#define GL_TEXTURE_UPDATE_BARRIER_BIT      0x00000100
#endif

#ifndef GL_FRAMEBUFFER_BARRIER_BIT
#define GL_FRAMEBUFFER_BARRIER_BIT         0x00000400
#endif

#ifndef GL_ALL_BARRIER_BITS
#define GL_ALL_BARRIER_BITS               0xFFFFFFFF
#endif

#ifndef GL_VERTEX_PROGRAM_POINT_SIZE
#define GL_VERTEX_PROGRAM_POINT_SIZE      0x8642
#endif

#ifndef GL_POINT_SPRITE
#define GL_POINT_SPRITE                   0x8861
#endif

#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT                   0x0001
#endif

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT                  0x0002
#endif

#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE         0x9100
#endif

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#endif

#ifndef GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#endif

#ifndef GL_MAX_COMPUTE_WORK_GROUP_COUNT
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT   0x91BE
#endif

#ifndef GL_MAX_COMPUTE_WORK_GROUP_SIZE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE    0x91BF
#endif

#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#define GL_TEXTURE_CUBE_MAP_SEAMLESS      0x884F
#endif

#ifndef GL_CONTEXT_LOST
#define GL_CONTEXT_LOST                   0x0507
#endif

#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH          0x8741
#endif

#ifndef GL_NUM_PROGRAM_BINARY_FORMATS
#define GL_NUM_PROGRAM_BINARY_FORMATS     0x87FE
#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D                     0x806F
#endif

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R                 0x8072
#endif

#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE              0x84F5
#endif

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY               0x8C1A
#endif

#ifndef GL_MAX_ARRAY_TEXTURE_LAYERS
#define GL_MAX_ARRAY_TEXTURE_LAYERS       0x88FF
#endif

#ifndef GL_MAX_VERTEX_UNIFORM_COMPONENTS
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS  0x8B4A
#endif

#ifndef GL_MAX_FRAGMENT_UNIFORM_COMPONENTS
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS 0x8B49
#endif

#ifndef GL_MAX_VERTEX_UNIFORM_VECTORS
#define GL_MAX_VERTEX_UNIFORM_VECTORS     0x8DFB
#endif

#ifndef GL_MAX_FRAGMENT_UNIFORM_VECTORS
#define GL_MAX_FRAGMENT_UNIFORM_VECTORS   0x8DFD
#endif

#ifndef GL_RGB10_A2
#define GL_RGB10_A2                       0x8059
#endif

#ifndef GL_UNSIGNED_INT_2_10_10_10_REV
#define GL_UNSIGNED_INT_2_10_10_10_REV    0x8368
#endif

#ifndef GL_MAX_VARYING_COMPONENTS
#define GL_MAX_VARYING_COMPONENTS         0x8B4B
#endif

#ifndef GL_MAX_VARYING_FLOATS
#define GL_MAX_VARYING_FLOATS             0x8B4B
#endif

#ifndef GL_MAX_VARYING_VECTORS
#define GL_MAX_VARYING_VECTORS            0x8DFC
#endif

#ifndef GL_TESS_CONTROL_SHADER
#define GL_TESS_CONTROL_SHADER            0x8E88
#endif

#ifndef GL_TESS_EVALUATION_SHADER
#define GL_TESS_EVALUATION_SHADER         0x8E87
#endif

#ifndef GL_PATCH_VERTICES
#define GL_PATCH_VERTICES                 0x8E72
#endif

#ifndef GL_LINE
#define GL_LINE                           0x1B01
#endif

#ifndef GL_FILL
#define GL_FILL                           0x1B02
#endif

#ifndef GL_PATCHES
#define GL_PATCHES                        0x000E
#endif

#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER                0x8DD9
#endif

QRhiTexture::Format QSGRhiSupport::toRhiTextureFormatFromGL(uint format)
{
    auto rhiFormat = QRhiTexture::UnknownFormat;
    switch (format) {
    case GL_RGBA:
        Q_FALLTHROUGH();
    case GL_RGBA8:
        rhiFormat = QRhiTexture::RGBA8;
        break;
    case GL_BGRA:
        rhiFormat = QRhiTexture::BGRA8;
        break;
    case GL_R16:
        rhiFormat = QRhiTexture::R16;
        break;
    case GL_RG16:
        rhiFormat = QRhiTexture::RG16;
        break;
    case GL_RED:
        Q_FALLTHROUGH();
    case GL_R8:
        rhiFormat = QRhiTexture::R8;
        break;
    case GL_RG:
        Q_FALLTHROUGH();
    case GL_RG8:
        rhiFormat = QRhiTexture::RG8;
        break;
    case GL_ALPHA:
        rhiFormat = QRhiTexture::RED_OR_ALPHA8;
        break;
    case GL_RGBA16F:
        rhiFormat = QRhiTexture::RGBA16F;
        break;
    case GL_RGBA32F:
        rhiFormat = QRhiTexture::RGBA32F;
        break;
    case GL_R16F:
        rhiFormat = QRhiTexture::R16F;
        break;
    case GL_R32F:
        rhiFormat = QRhiTexture::R32F;
        break;
    case GL_RGB10_A2:
        rhiFormat = QRhiTexture::RGB10A2;
        break;
    case GL_DEPTH_COMPONENT:
        Q_FALLTHROUGH();
    case GL_DEPTH_COMPONENT16:
        rhiFormat = QRhiTexture::D16;
        break;
    case GL_DEPTH_COMPONENT24:
        rhiFormat = QRhiTexture::D24;
        break;
    case GL_DEPTH_STENCIL:
        Q_FALLTHROUGH();
    case GL_DEPTH24_STENCIL8:
        rhiFormat = QRhiTexture::D24S8;
        break;
    case GL_DEPTH_COMPONENT32F:
        rhiFormat = QRhiTexture::D32F;
        break;
    default:
        qWarning("GL format %d is not supported", format);
        break;
    }
    return rhiFormat;
}
#endif

#if QT_CONFIG(vulkan)
QRhiTexture::Format QSGRhiSupport::toRhiTextureFormatFromVulkan(uint format, QRhiTexture::Flags *flags)
{
    auto rhiFormat = QRhiTexture::UnknownFormat;
    bool sRGB = false;
    switch (format) {
    case VK_FORMAT_R8G8B8A8_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_R8G8B8A8_UNORM:
        rhiFormat = QRhiTexture::RGBA8;
        break;
    case VK_FORMAT_B8G8R8A8_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_B8G8R8A8_UNORM:
        rhiFormat = QRhiTexture::BGRA8;
        break;
    case VK_FORMAT_R8_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_R8_UNORM:
        rhiFormat = QRhiTexture::R8;
        break;
    case VK_FORMAT_R8G8_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_R8G8_UNORM:
        rhiFormat = QRhiTexture::RG8;
        break;
    case VK_FORMAT_R16_UNORM:
        rhiFormat = QRhiTexture::R16;
        break;
    case VK_FORMAT_R16G16_UNORM:
        rhiFormat = QRhiTexture::RG16;
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        rhiFormat = QRhiTexture::RGBA16F;
        break;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        rhiFormat = QRhiTexture::RGBA32F;
        break;
    case VK_FORMAT_R16_SFLOAT:
        rhiFormat = QRhiTexture::R16F;
        break;
    case VK_FORMAT_R32_SFLOAT:
        rhiFormat = QRhiTexture::R32F;
        break;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: // intentionally
        Q_FALLTHROUGH();
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
        rhiFormat = QRhiTexture::RGB10A2;
        break;
    case VK_FORMAT_D16_UNORM:
        rhiFormat = QRhiTexture::D16;
        break;
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        rhiFormat = QRhiTexture::D24;
        break;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        rhiFormat = QRhiTexture::D24S8;
        break;
    case VK_FORMAT_D32_SFLOAT:
        rhiFormat = QRhiTexture::D32F;
        break;
    case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        rhiFormat = QRhiTexture::BC1;
        break;
    case VK_FORMAT_BC2_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_BC2_UNORM_BLOCK:
        rhiFormat = QRhiTexture::BC2;
        break;
    case VK_FORMAT_BC3_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_BC3_UNORM_BLOCK:
        rhiFormat = QRhiTexture::BC3;
        break;
    case VK_FORMAT_BC4_UNORM_BLOCK:
        rhiFormat = QRhiTexture::BC4;
        break;
    case VK_FORMAT_BC5_UNORM_BLOCK:
        rhiFormat = QRhiTexture::BC5;
        break;
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        rhiFormat = QRhiTexture::BC6H;
        break;
    case VK_FORMAT_BC7_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_BC7_UNORM_BLOCK:
        rhiFormat = QRhiTexture::BC7;
        break;
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ETC2_RGB8;
        break;
    case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ETC2_RGB8A1;
        break;
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ETC2_RGBA8;
        break;
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_4x4;
        break;
    case VK_FORMAT_ASTC_5x4_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_5x4;
        break;
    case VK_FORMAT_ASTC_5x5_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_5x5;
        break;
    case VK_FORMAT_ASTC_6x5_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_6x5;
        break;
    case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_6x6;
        break;
    case VK_FORMAT_ASTC_8x5_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_8x5;
        break;
    case VK_FORMAT_ASTC_8x6_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_8x6;
        break;
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_8x8;
        break;
    case VK_FORMAT_ASTC_10x5_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_10x5;
        break;
    case VK_FORMAT_ASTC_10x6_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_10x6;
        break;
    case VK_FORMAT_ASTC_10x8_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_10x8;
        break;
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_10x10;
        break;
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_12x10;
        break;
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK:
        sRGB = true;
        Q_FALLTHROUGH();
    case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        rhiFormat = QRhiTexture::ASTC_12x12;
        break;
    default:
        qWarning("VkFormat %d is not supported", format);
        break;
    }
    if (sRGB)
        (*flags) |=(QRhiTexture::sRGB);
    return rhiFormat;
}
#endif

#ifdef Q_OS_WIN
QRhiTexture::Format QSGRhiSupport::toRhiTextureFormatFromD3D11(uint format, QRhiTexture::Flags *flags)
{
    auto rhiFormat = QRhiTexture::UnknownFormat;
    bool sRGB = false;
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        rhiFormat = QRhiTexture::RGBA8;
        break;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        rhiFormat = QRhiTexture::BGRA8;
        break;
    case DXGI_FORMAT_R8_UNORM:
        rhiFormat = QRhiTexture::R8;
        break;
    case DXGI_FORMAT_R8G8_UNORM:
        rhiFormat = QRhiTexture::RG8;
        break;
    case DXGI_FORMAT_R16_UNORM:
        rhiFormat = QRhiTexture::R16;
        break;
    case DXGI_FORMAT_R16G16_UNORM:
        rhiFormat = QRhiTexture::RG16;
        break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        rhiFormat = QRhiTexture::RGBA16F;
        break;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        rhiFormat = QRhiTexture::RGBA32F;
        break;
    case DXGI_FORMAT_R16_FLOAT:
        rhiFormat = QRhiTexture::R16F;
        break;
    case DXGI_FORMAT_R32_FLOAT:
        rhiFormat = QRhiTexture::R32F;
        break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        rhiFormat = QRhiTexture::RGB10A2;
        break;
    case DXGI_FORMAT_R16_TYPELESS:
        rhiFormat = QRhiTexture::D16;
        break;
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        rhiFormat = QRhiTexture::D24;
        break;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        rhiFormat = QRhiTexture::D24S8;
        break;
    case DXGI_FORMAT_R32_TYPELESS:
        rhiFormat = QRhiTexture::D32F;
        break;
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case DXGI_FORMAT_BC1_UNORM:
        rhiFormat = QRhiTexture::BC1;
        break;
    case DXGI_FORMAT_BC2_UNORM_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case DXGI_FORMAT_BC2_UNORM:
        rhiFormat = QRhiTexture::BC2;
        break;
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case DXGI_FORMAT_BC3_UNORM:
        rhiFormat = QRhiTexture::BC3;
        break;
    case DXGI_FORMAT_BC4_UNORM:
        rhiFormat = QRhiTexture::BC4;
        break;
    case DXGI_FORMAT_BC5_UNORM:
        rhiFormat = QRhiTexture::BC5;
        break;
    case DXGI_FORMAT_BC6H_UF16:
        rhiFormat = QRhiTexture::BC6H;
        break;
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        sRGB = true;
        Q_FALLTHROUGH();
    case DXGI_FORMAT_BC7_UNORM:
        rhiFormat = QRhiTexture::BC7;
        break;
    default:
        qWarning("DXGI_FORMAT %d is not supported", format);
        break;
    }
    if (sRGB)
        (*flags) |=(QRhiTexture::sRGB);
    return rhiFormat;
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
namespace QSGRhiSupportMac {
    QRhiTexture::Format toRhiTextureFormatFromMetal(uint format, QRhiTexture::Flags *flags);
}
QRhiTexture::Format QSGRhiSupport::toRhiTextureFormatFromMetal(uint format, QRhiTexture::Flags *flags)
{
    return QSGRhiSupportMac::toRhiTextureFormatFromMetal(format, flags);
}
#endif

void QSGRhiSupport::configure(QSGRendererInterface::GraphicsApi api)
{
    if (api == QSGRendererInterface::Unknown) {
        // behave as if nothing was explicitly requested
        m_requested.valid = false;
        applySettings();
    } else {
        Q_ASSERT(QSGRendererInterface::isApiRhiBased(api));
        m_requested.valid = true;
        m_requested.api = api;
        applySettings();
    }
}

QSGRhiSupport *QSGRhiSupport::instance_internal()
{
    static QSGRhiSupport inst;
    return &inst;
}

QSGRhiSupport *QSGRhiSupport::instance()
{
    QSGRhiSupport *inst = instance_internal();
    if (!inst->m_settingsApplied)
        inst->applySettings();
    return inst;
}

QString QSGRhiSupport::rhiBackendName() const
{
    switch (m_rhiBackend) {
    case QRhi::Null:
        return QLatin1String("Null");
    case QRhi::Vulkan:
        return QLatin1String("Vulkan");
    case QRhi::OpenGLES2:
        return QLatin1String("OpenGL");
    case QRhi::D3D11:
        return QLatin1String("D3D11");
    case QRhi::Metal:
        return QLatin1String("Metal");
    default:
        return QLatin1String("Unknown");
    }
}

QSGRendererInterface::GraphicsApi QSGRhiSupport::graphicsApi() const
{
    switch (m_rhiBackend) {
    case QRhi::Null:
        return QSGRendererInterface::NullRhi;
    case QRhi::Vulkan:
        return QSGRendererInterface::VulkanRhi;
    case QRhi::OpenGLES2:
        return QSGRendererInterface::OpenGLRhi;
    case QRhi::D3D11:
        return QSGRendererInterface::Direct3D11Rhi;
    case QRhi::Metal:
        return QSGRendererInterface::MetalRhi;
    default:
        return QSGRendererInterface::Unknown;
    }
}

QSurface::SurfaceType QSGRhiSupport::windowSurfaceType() const
{
    switch (m_rhiBackend) {
    case QRhi::Vulkan:
        return QSurface::VulkanSurface;
    case QRhi::OpenGLES2:
        return QSurface::OpenGLSurface;
    case QRhi::D3D11:
        return QSurface::Direct3DSurface;
    case QRhi::Metal:
        return QSurface::MetalSurface;
    default:
        return QSurface::OpenGLSurface;
    }
}

#if QT_CONFIG(vulkan)
static const void *qsgrhi_vk_rifResource(QSGRendererInterface::Resource res,
                                         const QRhiNativeHandles *nat,
                                         const QRhiNativeHandles *cbNat,
                                         const QRhiNativeHandles *rpNat)
{
    const QRhiVulkanNativeHandles *vknat = static_cast<const QRhiVulkanNativeHandles *>(nat);
    const QRhiVulkanCommandBufferNativeHandles *maybeVkCbNat =
            static_cast<const QRhiVulkanCommandBufferNativeHandles *>(cbNat);
    const QRhiVulkanRenderPassNativeHandles *maybeVkRpNat =
            static_cast<const QRhiVulkanRenderPassNativeHandles *>(rpNat);

    switch (res) {
    case QSGRendererInterface::DeviceResource:
        return &vknat->dev;
    case QSGRendererInterface::CommandQueueResource:
        return &vknat->gfxQueue;
    case QSGRendererInterface::CommandListResource:
        if (maybeVkCbNat)
            return &maybeVkCbNat->commandBuffer;
        else
            return nullptr;
    case QSGRendererInterface::PhysicalDeviceResource:
        return &vknat->physDev;
    case QSGRendererInterface::RenderPassResource:
        if (maybeVkRpNat)
            return &maybeVkRpNat->renderPass;
        else
            return nullptr;
    default:
        return nullptr;
    }
}
#endif

#if QT_CONFIG(opengl)
static const void *qsgrhi_gl_rifResource(QSGRendererInterface::Resource res, const QRhiNativeHandles *nat)
{
    const QRhiGles2NativeHandles *glnat = static_cast<const QRhiGles2NativeHandles *>(nat);
    switch (res) {
    case QSGRendererInterface::OpenGLContextResource:
        return glnat->context;
    default:
        return nullptr;
    }
}
#endif

#ifdef Q_OS_WIN
static const void *qsgrhi_d3d11_rifResource(QSGRendererInterface::Resource res, const QRhiNativeHandles *nat)
{
    const QRhiD3D11NativeHandles *d3dnat = static_cast<const QRhiD3D11NativeHandles *>(nat);
    switch (res) {
    case QSGRendererInterface::DeviceResource:
        return d3dnat->dev;
    case QSGRendererInterface::DeviceContextResource:
        return d3dnat->context;
    default:
        return nullptr;
    }
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
static const void *qsgrhi_mtl_rifResource(QSGRendererInterface::Resource res, const QRhiNativeHandles *nat,
                                    const QRhiNativeHandles *cbNat)
{
    const QRhiMetalNativeHandles *mtlnat = static_cast<const QRhiMetalNativeHandles *>(nat);
    const QRhiMetalCommandBufferNativeHandles *maybeMtlCbNat =
            static_cast<const QRhiMetalCommandBufferNativeHandles *>(cbNat);

    switch (res) {
    case QSGRendererInterface::DeviceResource:
        return mtlnat->dev;
    case QSGRendererInterface::CommandQueueResource:
        return mtlnat->cmdQueue;
    case QSGRendererInterface::CommandListResource:
        if (maybeMtlCbNat)
            return maybeMtlCbNat->commandBuffer;
        else
            return nullptr;
    case QSGRendererInterface::CommandEncoderResource:
        if (maybeMtlCbNat)
            return maybeMtlCbNat->encoder;
        else
            return nullptr;
    default:
        return nullptr;
    }
}
#endif

const void *QSGRhiSupport::rifResource(QSGRendererInterface::Resource res,
                                       const QSGDefaultRenderContext *rc,
                                       const QQuickWindow *w)
{
    QRhi *rhi = rc->rhi();
    if (!rhi)
        return nullptr;

    // Accessing the underlying QRhi* objects are essential both for Qt Quick
    // 3D and advanced solutions, such as VR engine integrations.
    switch (res) {
    case QSGRendererInterface::RhiResource:
        return rhi;
    case QSGRendererInterface::RhiSwapchainResource:
        return QQuickWindowPrivate::get(w)->swapchain;
    case QSGRendererInterface::RhiRedirectCommandBuffer:
        return QQuickWindowPrivate::get(w)->redirect.commandBuffer;
    case QSGRendererInterface::RhiRedirectRenderTarget:
        return QQuickWindowPrivate::get(w)->redirect.rt.renderTarget;
    default:
        break;
    }

    const QRhiNativeHandles *nat = rhi->nativeHandles();
    if (!nat)
        return nullptr;

    switch (m_rhiBackend) {
#if QT_CONFIG(vulkan)
    case QRhi::Vulkan:
    {
        QRhiCommandBuffer *cb = rc->currentFrameCommandBuffer();
        QRhiRenderPassDescriptor *rp = rc->currentFrameRenderPass();
        return qsgrhi_vk_rifResource(res, nat,
                                     cb ? cb->nativeHandles() : nullptr,
                                     rp ? rp->nativeHandles() : nullptr);
    }
#endif
#if QT_CONFIG(opengl)
    case QRhi::OpenGLES2:
        return qsgrhi_gl_rifResource(res, nat);
#endif
#ifdef Q_OS_WIN
    case QRhi::D3D11:
        return qsgrhi_d3d11_rifResource(res, nat);
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    case QRhi::Metal:
    {
        QRhiCommandBuffer *cb = rc->currentFrameCommandBuffer();
        return qsgrhi_mtl_rifResource(res, nat, cb ? cb->nativeHandles() : nullptr);
    }
#endif
    default:
        return nullptr;
    }
}

int QSGRhiSupport::chooseSampleCount(int samples, QRhi *rhi)
{
    int msaaSampleCount = samples;
    if (qEnvironmentVariableIsSet("QSG_SAMPLES"))
        msaaSampleCount = qEnvironmentVariableIntValue("QSG_SAMPLES");
    msaaSampleCount = qMax(1, msaaSampleCount);
    if (msaaSampleCount > 1) {
        const QVector<int> supportedSampleCounts = rhi->supportedSampleCounts();
        if (!supportedSampleCounts.contains(msaaSampleCount)) {
            int reducedSampleCount = 1;
            for (int i = supportedSampleCounts.count() - 1; i >= 0; --i) {
                if (supportedSampleCounts[i] <= msaaSampleCount) {
                    reducedSampleCount = supportedSampleCounts[i];
                    break;
                }
            }
            qWarning() << "Requested MSAA sample count" << msaaSampleCount
                       << "but supported sample counts are" << supportedSampleCounts
                       << ", using sample count" << reducedSampleCount << "instead";
            msaaSampleCount = reducedSampleCount;
        }
    }
    return msaaSampleCount;
}

int QSGRhiSupport::chooseSampleCountForWindowWithRhi(QWindow *window, QRhi *rhi)
{
    return chooseSampleCount(qMax(QSurfaceFormat::defaultFormat().samples(), window->requestedFormat().samples()), rhi);
}

// must be called on the main thread
QOffscreenSurface *QSGRhiSupport::maybeCreateOffscreenSurface(QWindow *window)
{
    QOffscreenSurface *offscreenSurface = nullptr;
#if QT_CONFIG(opengl)
    if (rhiBackend() == QRhi::OpenGLES2) {
        const QSurfaceFormat format = window->requestedFormat();
        offscreenSurface = QRhiGles2InitParams::newFallbackSurface(format);
    }
#else
    Q_UNUSED(window);
#endif
    return offscreenSurface;
}

void QSGRhiSupport::prepareWindowForRhi(QQuickWindow *window)
{
#if QT_CONFIG(vulkan)
    if (rhiBackend() == QRhi::Vulkan) {
        QQuickWindowPrivate *wd = QQuickWindowPrivate::get(window);
        // QQuickWindows must get a QVulkanInstance automatically (it is
        // created when the first window is constructed and is destroyed only
        // on exit), unless the application decided to set its own. With
        // QQuickRenderControl, no QVulkanInstance is created, because it must
        // always be under the application's control then (since the default
        // instance we could create here would not be configurable by the
        // application in any way, and that is often not acceptable).
        if (!window->vulkanInstance() && !wd->renderControl) {
            QVulkanInstance *vkinst = QVulkanDefaultInstance::instance();
            if (vkinst)
                qCDebug(QSG_LOG_INFO) << "Got Vulkan instance from QVulkanDefaultInstance, requested api version was" << vkinst->apiVersion();
            else
                qCDebug(QSG_LOG_INFO) << "No Vulkan instance from QVulkanDefaultInstance, expect problems";
            window->setVulkanInstance(vkinst);
        }
    }
#else
    Q_UNUSED(window);
#endif
}

void QSGRhiSupport::preparePipelineCache(QRhi *rhi)
{
    if (m_pipelineCacheLoad.isEmpty())
        return;

    QFile f(m_pipelineCacheLoad);
    if (f.open(QIODevice::ReadOnly)) {
        qCDebug(QSG_LOG_INFO, "Attempting to seed pipeline cache from '%s'",
                qPrintable(m_pipelineCacheLoad));
        rhi->setPipelineCacheData(f.readAll());
    } else {
        qWarning("Could not open pipeline cache source file '%s'",
                 qPrintable(m_pipelineCacheLoad));
    }
}

// must be called on the render thread
QSGRhiSupport::RhiCreateResult QSGRhiSupport::createRhi(QQuickWindow *window, QSurface *offscreenSurface)
{
    QRhi *rhi = nullptr;
    QQuickWindowPrivate *wd = QQuickWindowPrivate::get(window);
    const QQuickGraphicsDevicePrivate *customDevD = QQuickGraphicsDevicePrivate::get(&wd->customDeviceObjects);
    if (customDevD->type == QQuickGraphicsDevicePrivate::Type::Rhi) {
        rhi = customDevD->u.rhi;
        if (rhi) {
            preparePipelineCache(rhi);
            return { rhi, false };
        }
    }

    QRhi::Flags flags;
    if (isProfilingRequested())
        flags |= QRhi::EnableProfiling | QRhi::EnableDebugMarkers;
    if (isSoftwareRendererRequested())
        flags |= QRhi::PreferSoftwareRenderer;
    if (!m_pipelineCacheSave.isEmpty())
        flags |= QRhi::EnablePipelineCacheDataSave;

    const QRhi::Implementation backend = rhiBackend();
    if (backend == QRhi::Null) {
        QRhiNullInitParams rhiParams;
        rhi = QRhi::create(backend, &rhiParams, flags);
    }
#if QT_CONFIG(opengl)
    if (backend == QRhi::OpenGLES2) {
        const QSurfaceFormat format = window->requestedFormat();
        QRhiGles2InitParams rhiParams;
        rhiParams.format = format;
        rhiParams.fallbackSurface = offscreenSurface;
        rhiParams.window = window;
        if (customDevD->type == QQuickGraphicsDevicePrivate::Type::OpenGLContext) {
            QRhiGles2NativeHandles importDev;
            importDev.context = customDevD->u.context;
            qCDebug(QSG_LOG_INFO, "Using existing QOpenGLContext %p", importDev.context);
            rhi = QRhi::create(backend, &rhiParams, flags, &importDev);
        } else {
            rhi = QRhi::create(backend, &rhiParams, flags);
        }
    }
#else
    Q_UNUSED(offscreenSurface);
    if (backend == QRhi::OpenGLES2)
        qWarning("OpenGL was requested for Qt Quick, but this build of Qt has no OpenGL support.");
#endif
#if QT_CONFIG(vulkan)
    if (backend == QRhi::Vulkan) {
        if (isDebugLayerRequested())
            QVulkanDefaultInstance::setFlag(QVulkanDefaultInstance::EnableValidation, true);
        QRhiVulkanInitParams rhiParams;
        prepareWindowForRhi(window); // sets a vulkanInstance if not yet present
        rhiParams.inst = window->vulkanInstance();
        if (!rhiParams.inst)
            qWarning("No QVulkanInstance set for QQuickWindow, this is wrong.");
        if (window->handle()) // only used for vkGetPhysicalDeviceSurfaceSupportKHR and that implies having a valid native window
            rhiParams.window = window;
        rhiParams.deviceExtensions = wd->graphicsConfig.deviceExtensions();
        if (customDevD->type == QQuickGraphicsDevicePrivate::Type::DeviceObjects) {
            QRhiVulkanNativeHandles importDev;
            importDev.physDev = reinterpret_cast<VkPhysicalDevice>(customDevD->u.deviceObjects.physicalDevice);
            importDev.dev = reinterpret_cast<VkDevice>(customDevD->u.deviceObjects.device);
            importDev.gfxQueueFamilyIdx = customDevD->u.deviceObjects.queueFamilyIndex;
            importDev.gfxQueueIdx = customDevD->u.deviceObjects.queueIndex;
            qCDebug(QSG_LOG_INFO, "Using existing native Vulkan physical device %p device %p graphics queue family index %d",
                    importDev.physDev, importDev.dev, importDev.gfxQueueFamilyIdx);
            rhi = QRhi::create(backend, &rhiParams, flags, &importDev);
        } else if (customDevD->type == QQuickGraphicsDevicePrivate::Type::PhysicalDevice) {
            QRhiVulkanNativeHandles importDev;
            importDev.physDev = reinterpret_cast<VkPhysicalDevice>(customDevD->u.physicalDevice.physicalDevice);
            qCDebug(QSG_LOG_INFO, "Using existing native Vulkan physical device %p", importDev.physDev);
            rhi = QRhi::create(backend, &rhiParams, flags, &importDev);
        } else {
            rhi = QRhi::create(backend, &rhiParams, flags);
        }
    }
#else
    if (backend == QRhi::Vulkan)
        qWarning("Vulkan was requested for Qt Quick, but this build of Qt has no Vulkan support.");
#endif
#ifdef Q_OS_WIN
    if (backend == QRhi::D3D11) {
        QRhiD3D11InitParams rhiParams;
        rhiParams.enableDebugLayer = isDebugLayerRequested();
        if (m_killDeviceFrameCount > 0) {
            rhiParams.framesUntilKillingDeviceViaTdr = m_killDeviceFrameCount;
            rhiParams.repeatDeviceKill = true;
        }
        if (customDevD->type == QQuickGraphicsDevicePrivate::Type::DeviceAndContext) {
            QRhiD3D11NativeHandles importDev;
            importDev.dev = customDevD->u.deviceAndContext.device;
            importDev.context = customDevD->u.deviceAndContext.context;
            qCDebug(QSG_LOG_INFO, "Using existing native D3D11 device %p and context %p",
                    importDev.dev, importDev.context);
            rhi = QRhi::create(backend, &rhiParams, flags, &importDev);
        } else if (customDevD->type == QQuickGraphicsDevicePrivate::Type::Adapter) {
            QRhiD3D11NativeHandles importDev;
            importDev.adapterLuidLow = customDevD->u.adapter.luidLow;
            importDev.adapterLuidHigh = customDevD->u.adapter.luidHigh;
            importDev.featureLevel = customDevD->u.adapter.featureLevel;
            qCDebug(QSG_LOG_INFO, "Using D3D11 adapter LUID %u, %d and feature level %d",
                    importDev.adapterLuidLow, importDev.adapterLuidHigh, importDev.featureLevel);
            rhi = QRhi::create(backend, &rhiParams, flags, &importDev);
        } else {
            rhi = QRhi::create(backend, &rhiParams, flags);
            if (!rhi && !flags.testFlag(QRhi::PreferSoftwareRenderer)) {
                qCDebug(QSG_LOG_INFO, "Failed to create a D3D device with default settings; "
                                      "attempting to get a software rasterizer backed device instead");
                flags |= QRhi::PreferSoftwareRenderer;
                rhi = QRhi::create(backend, &rhiParams, flags);
            }
        }
    }
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    if (backend == QRhi::Metal) {
        QRhiMetalInitParams rhiParams;
        if (customDevD->type == QQuickGraphicsDevicePrivate::Type::DeviceAndCommandQueue) {
            QRhiMetalNativeHandles importDev;
            importDev.dev = (MTLDevice *) customDevD->u.deviceAndCommandQueue.device;
            importDev.cmdQueue = (MTLCommandQueue *) customDevD->u.deviceAndCommandQueue.cmdQueue;
            qCDebug(QSG_LOG_INFO, "Using existing native Metal device %p and command queue %p",
                    importDev.dev, importDev.cmdQueue);
            rhi = QRhi::create(backend, &rhiParams, flags, &importDev);
        } else {
            rhi = QRhi::create(backend, &rhiParams, flags);
        }
    }
#endif

    if (rhi)
        preparePipelineCache(rhi);
    else
        qWarning("Failed to create RHI (backend %d)", backend);

    return { rhi, true };
}

void QSGRhiSupport::destroyRhi(QRhi *rhi)
{
    if (!rhi)
        return;

    if (!rhi->isDeviceLost()) {
        if (!m_pipelineCacheSave.isEmpty()) {
            QFile f(m_pipelineCacheSave);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                qCDebug(QSG_LOG_INFO, "Writing pipeline cache contents to '%s'",
                        qPrintable(m_pipelineCacheSave));
                f.write(rhi->pipelineCacheData());
            } else {
                qWarning("Could not open pipeline cache output file '%s'",
                         qPrintable(m_pipelineCacheSave));
            }
        }
    }

    delete rhi;
}

QImage QSGRhiSupport::grabAndBlockInCurrentFrame(QRhi *rhi, QRhiCommandBuffer *cb, QRhiTexture *src)
{
    Q_ASSERT(rhi->isRecordingFrame());

    QRhiReadbackResult result;
    QRhiReadbackDescription readbackDesc(src); // null src == read from swapchain backbuffer
    QRhiResourceUpdateBatch *resourceUpdates = rhi->nextResourceUpdateBatch();
    resourceUpdates->readBackTexture(readbackDesc, &result);

    cb->resourceUpdate(resourceUpdates);
    rhi->finish(); // make sure the readback has finished, stall the pipeline if needed

    // May be RGBA or BGRA. Plus premultiplied alpha.
    QImage::Format imageFormat;
    if (result.format == QRhiTexture::BGRA8) {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        imageFormat = QImage::Format_ARGB32_Premultiplied;
#else
        imageFormat = QImage::Format_RGBA8888_Premultiplied;
        // ### and should swap too
#endif
    } else {
        imageFormat = QImage::Format_RGBA8888_Premultiplied;
    }

    const uchar *p = reinterpret_cast<const uchar *>(result.data.constData());
    const QImage img(p, result.pixelSize.width(), result.pixelSize.height(), imageFormat);

    if (rhi->isYUpInFramebuffer())
        return img.mirrored();

    return img.copy();
}

QImage QSGRhiSupport::grabOffscreen(QQuickWindow *window)
{
    // Set up and then tear down the entire rendering infrastructure. This
    // function is called on the gui/main thread - but that's alright because
    // there is no onscreen rendering initialized at this point (so no render
    // thread for instance).

    QQuickWindowPrivate *wd = QQuickWindowPrivate::get(window);
    // It is expected that window is not using QQuickRenderControl, i.e. it is
    // a normal QQuickWindow that just happens to be not exposed.
    Q_ASSERT(!wd->renderControl);

    QScopedPointer<QOffscreenSurface> offscreenSurface(maybeCreateOffscreenSurface(window));
    RhiCreateResult rhiResult = createRhi(window, offscreenSurface.data());
    if (!rhiResult.rhi) {
        qWarning("Failed to initialize QRhi for offscreen readback");
        return QImage();
    }
    std::unique_ptr<QRhi> rhiOwner(rhiResult.rhi);
    QRhi *rhi = rhiResult.own ? rhiOwner.get() : rhiOwner.release();

    const QSize pixelSize = window->size() * window->devicePixelRatio();
    QScopedPointer<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1,
                                                        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!texture->create()) {
        qWarning("Failed to build texture for offscreen readback");
        return QImage();
    }
    QScopedPointer<QRhiRenderBuffer> depthStencil(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!depthStencil->create()) {
        qWarning("Failed to create depth/stencil buffer for offscreen readback");
        return QImage();
    }
    QRhiTextureRenderTargetDescription rtDesc(texture.data());
    rtDesc.setDepthStencilBuffer(depthStencil.data());
    QScopedPointer<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    QScopedPointer<QRhiRenderPassDescriptor> rpDesc(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rpDesc.data());
    if (!rt->create()) {
        qWarning("Failed to build render target for offscreen readback");
        return QImage();
    }

    wd->rhi = rhi;

    QSGDefaultRenderContext::InitParams params;
    params.rhi = rhi;
    params.sampleCount = 1;
    params.initialSurfacePixelSize = pixelSize;
    params.maybeSurface = window;
    wd->context->initialize(&params);

    // There was no rendercontrol which means a custom render target
    // should not be set either. Set our own, temporarily.
    window->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(rt.data()));

    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        qWarning("Failed to start recording the frame for offscreen readback");
        return QImage();
    }

    wd->setCustomCommandBuffer(cb);
    wd->polishItems();
    wd->syncSceneGraph();
    wd->renderSceneGraph(window->size());
    wd->setCustomCommandBuffer(nullptr);

    QImage image = grabAndBlockInCurrentFrame(rhi, cb, texture.data());
    rhi->endOffscreenFrame();

    image.setDevicePixelRatio(window->devicePixelRatio());
    wd->cleanupNodesOnShutdown();
    wd->context->invalidate();

    window->setRenderTarget(QQuickRenderTarget());
    wd->rhi = nullptr;

    return image;
}

#ifdef Q_OS_WEBOS
QImage QSGRhiSupport::grabOffscreenForProtectedContent(QQuickWindow *window)
{
    // If a context is created for protected content, grabbing GPU
    // resources are restricted. For the case, normal context
    // and surface are needed to allow CPU access.
    // So dummy offscreen window is used here
    // This function is called in rendering thread.

    QScopedPointer<QQuickWindow> offscreenWindow;
    QQuickWindowPrivate *wd = QQuickWindowPrivate::get(window);
    // It is expected that window is not using QQuickRenderControl, i.e. it is
    // a normal QQuickWindow that just happens to be not exposed.
    Q_ASSERT(!wd->renderControl);

    // If context and surface are created for protected content,
    // CPU can't read the frame resources. So normal context and surface are needed.
    if (window->requestedFormat().testOption(QSurfaceFormat::ProtectedContent)) {
        QSurfaceFormat surfaceFormat = window->requestedFormat();
        surfaceFormat.setOption(QSurfaceFormat::ProtectedContent, false);
        offscreenWindow.reset(new QQuickWindow());
        offscreenWindow->setFormat(surfaceFormat);
    }

    QScopedPointer<QOffscreenSurface> offscreenSurface(maybeCreateOffscreenSurface(window));
    RhiCreateResult rhiResult = createRhi(offscreenWindow.data() ? offscreenWindow.data() : window, offscreenSurface.data());
    if (!rhiResult.rhi) {
        qWarning("Failed to initialize QRhi for offscreen readback");
        return QImage();
    }
    QScopedPointer<QRhi> rhiOwner(rhiResult.rhi);
    QRhi *rhi = rhiResult.own ? rhiOwner.data() : rhiOwner.take();

    const QSize pixelSize = window->size() * window->devicePixelRatio();
    QScopedPointer<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1,
                                                        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!texture->create()) {
        qWarning("Failed to build texture for offscreen readback");
        return QImage();
    }
    QScopedPointer<QRhiRenderBuffer> depthStencil(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!depthStencil->create()) {
        qWarning("Failed to create depth/stencil buffer for offscreen readback");
        return QImage();
    }
    QRhiTextureRenderTargetDescription rtDesc(texture.data());
    rtDesc.setDepthStencilBuffer(depthStencil.data());
    QScopedPointer<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    QScopedPointer<QRhiRenderPassDescriptor> rpDesc(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rpDesc.data());
    if (!rt->create()) {
        qWarning("Failed to build render target for offscreen readback");
        return QImage();
    }

    // Backup the original Rhi
    QRhi *currentRhi = wd->rhi;
    wd->rhi = rhi;

    QSGDefaultRenderContext::InitParams params;
    params.rhi = rhi;
    params.sampleCount = 1;
    params.initialSurfacePixelSize = pixelSize;
    params.maybeSurface = window;
    wd->context->initialize(&params);

    // Backup the original RenderTarget
    QQuickRenderTarget currentRenderTarget = window->renderTarget();
    // There was no rendercontrol which means a custom render target
    // should not be set either. Set our own, temporarily.
    window->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(rt.data()));

    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        qWarning("Failed to start recording the frame for offscreen readback");
        return QImage();
    }

    wd->setCustomCommandBuffer(cb);
    wd->polishItems();
    wd->syncSceneGraph();
    wd->renderSceneGraph(window->size());
    wd->setCustomCommandBuffer(nullptr);

    QImage image = grabAndBlockInCurrentFrame(rhi, cb, texture.data());
    rhi->endOffscreenFrame();

    image.setDevicePixelRatio(window->devicePixelRatio());

    // Called from gui/main thread on no onscreen rendering initialized
    if (!currentRhi) {
        wd->cleanupNodesOnShutdown();
        wd->context->invalidate();

        window->setRenderTarget(QQuickRenderTarget());
        wd->rhi = nullptr;
    } else {
        // Called from rendering thread for protected content
        // Restore to original Rhi, RenderTarget and Context
        window->setRenderTarget(currentRenderTarget);
        wd->rhi = currentRhi;
        params.rhi = currentRhi;
        wd->context->initialize(&params);
    }

    return image;
}
#endif

void QSGRhiSupport::applySwapChainFormat(QRhiSwapChain *scWithWindowSet)
{
    const char *fmtStr = "unknown";
    switch (m_swapChainFormat) {
    case QRhiSwapChain::SDR:
        fmtStr = "SDR";
        break;
    case QRhiSwapChain::HDRExtendedSrgbLinear:
        fmtStr = "scRGB";
        break;
    case QRhiSwapChain::HDR10:
        fmtStr = "HDR10";
        break;
    default:
        break;
    }

    if (!scWithWindowSet->isFormatSupported(m_swapChainFormat)) {
        if (m_swapChainFormat != QRhiSwapChain::SDR) {
            qCDebug(QSG_LOG_INFO, "Requested a %s swapchain but it is reported to be unsupported with the current display(s). "
                                  "In multi-screen configurations make sure the window is located on a HDR-enabled screen. "
                                  "Request ignored, using SDR swapchain.", fmtStr);
        }
        return;
    }

    scWithWindowSet->setFormat(m_swapChainFormat);

    if (m_swapChainFormat != QRhiSwapChain::SDR) {
        qCDebug(QSG_LOG_INFO, "Creating %s swapchain", fmtStr);
        qCDebug(QSG_LOG_INFO) << "HDR output info:" << scWithWindowSet->hdrInfo();
    }
}

QT_END_NAMESPACE
