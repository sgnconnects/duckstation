// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once
#include "util/shadergen.h"
#include "postprocessing_shader.h"
#include <sstream>

namespace FrontendCommon {

class PostProcessingShaderGen : public ShaderGen
{
public:
  PostProcessingShaderGen(RenderAPI render_api, bool supports_dual_source_blend);
  ~PostProcessingShaderGen();

  std::string GeneratePostProcessingVertexShader(const PostProcessingShader& shader);
  std::string GeneratePostProcessingFragmentShader(const PostProcessingShader& shader);

private:
  void WriteUniformBuffer(std::stringstream& ss, const PostProcessingShader& shader, bool use_push_constants);
};

} // namespace FrontendCommon