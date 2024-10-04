// picoled_client.cpp : Defines the entry point for the application.
//

#include "picoled_client.h"

picoled::picoled()
{
	win = NULL;
	gfx = NULL;

	text_len = 0;
	text[0] = '\0';

	image_width = 0;
	image_height = 0;
	num_back_anim = 0;
	sock = INVALID_SOCKET;

#ifdef _WIN32
	// Initialize WinSock
	WSADATA d;
	if (WSAStartup(MAKEWORD(2, 2), &d)) {
		k3error::Handler("Could not initialize winsock", "picoled()");
		return;
	}
#endif

	// Window creation
	k3error::SetHandler(k3error_StdOutHandler);
	win = k3winObj::CreateWindowedWithFormat(WINDOW_TITLE, 0, 0, DEFAULT_WIN_WIDTH, DEFAULT_WIN_HEIGHT, k3fmt::RGBA8_UNORM, NUM_VIEWS, 1);
	if (win == NULL) {
		k3error::Handler("Could not create window", "picoled()");
		return;
	}
	gfx = win->GetGfx();
	if (gfx == NULL) {
		k3error::Handler("Could not get graphics handle", "picoled()");
		return;
	}
	cmd = gfx->CreateCmdBuf();
	if (cmd == NULL) {
		k3error::Handler("Could not create command buffer", "picoled()");
		return;
	}
	fence = gfx->CreateFence();
	if (fence == NULL) {
		k3error::Handler("Could not create fence", "picoled()");
		return;
	}

	// Other initialization
	k3bindingParam bind_param[2];
	bind_param[0].type = k3bindingType::VIEW_SET;
	bind_param[0].view_set_desc.type = k3shaderBindType::SRV;
	bind_param[0].view_set_desc.num_views = 1;
	bind_param[0].view_set_desc.reg = 0;
	bind_param[0].view_set_desc.space = 0;
	bind_param[0].view_set_desc.offset = 0;
	bind_param[1].type = k3bindingType::VIEW_SET;
	bind_param[1].view_set_desc.type = k3shaderBindType::SAMPLER;
	bind_param[1].view_set_desc.num_views = 1;
	bind_param[1].view_set_desc.reg = 0;
	bind_param[1].view_set_desc.space = 0;
	bind_param[1].view_set_desc.offset = 1;

	k3shaderBinding binding = gfx->CreateShaderBinding(2, bind_param, 0, NULL);
	if (binding == NULL) {
		k3error::Handler("Could not create shader bindings", "picoled()");
		return;
	}

	k3inputElement elem[1];
	elem[0].name = "POSITION";
	elem[0].index = 0;
	elem[0].format = k3fmt::RG32_FLOAT;
	elem[0].slot = 0;
	elem[0].offset = 0;
	elem[0].in_type = k3inputType::VERTEX;
	elem[0].instance_step = 0;

	k3shader vs, ps;
	vs = gfx->CreateShaderFromCompiledFile("passthru_vs.cso");
	if (vs == NULL) {
		k3error::Handler("Could not create vertex shader", "picoled()");
		return;
	}
	ps = gfx->CreateShaderFromCompiledFile("tex1_ps.cso");
	if (ps == NULL) {
		k3error::Handler("Could not create pixel shader", "picoled()");
		return;
	}

	k3gfxStateDesc state_desc = { 0 };
	state_desc.num_input_elements = 1;
	state_desc.input_elements = elem;
	state_desc.shader_binding = binding;
	state_desc.vertex_shader = vs;
	state_desc.pixel_shader = ps;
	state_desc.sample_mask = ~0;
	state_desc.rast_state.fill_mode = k3fill::SOLID;
	state_desc.rast_state.cull_mode = k3cull::NONE;
	state_desc.rast_state.front_counter_clockwise = true;
	state_desc.blend_state.blend_op[0].rt_write_mask = 0xf;
	state_desc.prim_type = k3primType::TRIANGLE;
	state_desc.num_render_targets = 1;
	state_desc.rtv_format[0] = k3fmt::RGBA8_UNORM;
	state_desc.msaa_samples = 1;
	main_state = gfx->CreateGfxState(&state_desc);

	k3resourceDesc rdesc = { 0 };
	rdesc.width = DEFAULT_IMAGE_DIM;
	rdesc.height = DEFAULT_IMAGE_DIM;
	rdesc.depth = 1;
	rdesc.mip_levels = 1;
	rdesc.num_samples = 1;
	rdesc.format = k3fmt::B5G6R5_UNORM;
	k3viewDesc vdesc = { 0 };
	gpu_image = gfx->CreateSurface(&rdesc, &vdesc, &vdesc, NULL);
	if (gpu_image == NULL) {
		k3error::Handler("Could not create gpu image surface", "picoled()");
		return;
	}

	cpu_image = gfx->CreateUploadImage();
	if (cpu_image == NULL) {
		k3error::Handler("Could not create cpu image surface", "picoled()");
		return;
	}
	cpu_image->SetDimensions(DEFAULT_IMAGE_DIM, DEFAULT_IMAGE_DIM, 1, k3fmt::B5G6R5_UNORM);

	k3uploadBuffer upbuf = gfx->CreateUploadBuffer();
	float* buf_data = (float*)upbuf->MapForWrite(3 * 2 * sizeof(float));
	buf_data[0] = -1.0f; buf_data[1] = 1.0f;
	buf_data[2] = 3.0f; buf_data[3] = 1.0f;
	buf_data[4] = -1.0f; buf_data[5] = -3.0f;
	upbuf->Unmap();
	k3bufferDesc bdesc = { 0 };
	bdesc.size = 3 * 2 * sizeof(float);
	bdesc.stride = 2 * sizeof(float);
	fullscreen_vb = gfx->CreateBuffer(&bdesc);
	if (fullscreen_vb == NULL) {
		k3error::Handler("Could not create vertex buffer", "picoled()");
		return;
	}

	k3samplerDesc sdesc = { 0 };
	sdesc.filter = k3texFilter::MIN_MAG_MIP_LINEAR;
	sdesc.addr_u = k3texAddr::CLAMP;
	sdesc.addr_v = k3texAddr::CLAMP;
	sdesc.addr_w = k3texAddr::CLAMP;
	main_sampler = gfx->CreateSampler(&sdesc);
	if (main_sampler == NULL) {
		k3error::Handler("Could not create sampler", "picoled()");
		return;
	}
 

	// fill cpu image with black color
	uint16_t* image_data = (uint16_t*)cpu_image->MapForWrite();
	memset(image_data, 0x0, cpu_image->GetPitch() * DEFAULT_IMAGE_DIM);
	cpu_image->Unmap();

	win->SetDisplayFunc(DisplayCallback);
	win->SetIdleFunc(DisplayCallback);
	win->SetKeyboardFunc(KeyboardCallback);

	// Upload initial data
	k3resource res;
	cmd->Reset();

	res = fullscreen_vb->GetResource();
	cmd->TransitionResource(res, k3resourceState::COPY_DEST);
	cmd->UploadBuffer(upbuf, res);
	cmd->TransitionResource(res, k3resourceState::SHADER_BUFFER);

	res = gpu_image->GetResource();
	cmd->TransitionResource(res, k3resourceState::COPY_DEST);
	cmd->UploadImage(cpu_image, res);
	cmd->TransitionResource(res, k3resourceState::SHADER_RESOURCE);

	cmd->Close();
	gfx->SubmitCmdBuf(cmd);

	// Make window visible
	win->SetDataPtr(this);
	win->SetCursorVisible(true);
	win->SetVisible(true);

	k3fontDesc fdesc = { 0 };
	fdesc.view_index = 1;
	fdesc.name = "..\\assets\\KarmaFuture.ttf";
	fdesc.point_size = 32.0f;
	fdesc.style = k3fontStyle::NORMAL;
	fdesc.weight = k3fontWeight::NORMAL;
	fdesc.cmd_buf = cmd;
	fdesc.format = k3fmt::RGBA8_UNORM;
	fdesc.transparent = false;
	main_font = gfx->CreateFont(&fdesc);

	gfx->WaitGpuIdle();
}

picoled::~picoled()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

void picoled::Display()
{
	float clear_color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
	k3renderTargets rt = { NULL };
	rt.render_targets[0] = win->GetBackBuffer();
	cmd->Reset();
	cmd->TransitionResource(rt.render_targets[0]->GetResource(), k3resourceState::RENDER_TARGET);
	cmd->ClearRenderTarget(rt.render_targets[0], clear_color, NULL);

	cmd->SetGfxState(main_state);
	cmd->SetVertexBuffer(0, fullscreen_vb);
	cmd->SetShaderView(0, gpu_image);
	cmd->SetSampler(1, main_sampler);
	cmd->SetDrawPrim(k3drawPrimType::TRIANGLELIST);
	cmd->SetViewToSurface(rt.render_targets[0]->GetResource());
	cmd->SetRenderTargets(&rt);
	cmd->Draw(3);

	if (text_len) {
		float text_fg_color[] = { 1.0f, 1.0f, 1.0f, 1.0f };
		float text_bg_color[] = { 0.0, 0.0, 0.0, 1.0 };
		cmd->DrawText(text, main_font, text_fg_color, text_bg_color, 0, 0, k3fontAlignment::BOTTOM_LEFT);
	}

	cmd->TransitionResource(rt.render_targets[0]->GetResource(), k3resourceState::COMMON);
	cmd->Close();
	gfx->SubmitCmdBuf(cmd);

	win->SwapBuffer();
}

void picoled::Keyboard(k3key k, char c, k3keyState state)
{
	if (c >= 32 && c <= 127 && state != k3keyState::PRESSED && text_len < MAX_TEXT_LEN - 1) {
		text[text_len] = c;
		text_len++;
		text[text_len] = '\0';
	}
	if (state == k3keyState::PRESSED) {
		switch (k) {
		case k3key::ESCAPE:
			text[0] = '\0';
			text_len = 0;
			break;
		case k3key::ENTER:
		case k3key::NUM_ENTER:
			if (text_len > 0) {
				ExecuteCommand();
				text[0] = '\0';
				text_len = 0;
			}
			break;
		}
	}
}

void picoled::SendArray8(uint32_t len, const uint8_t* data)
{
	send(sock, (const char*)data, len, 0);
}

void picoled::SendArray16(uint32_t len, const uint16_t* data)
{
	const uint16_t* d = data;
	uint32_t i;
	for (i = 0; i < len; i++, d++) {
		send(sock, ((const char*)d) + 1, 1, 0);
		send(sock, ((const char*)d), 1, 0);
	}
}

void picoled::SendArray32(uint32_t len, const uint32_t* data)
{
	const uint32_t* d = data;
	uint32_t i;
	for (i = 0; i < len; i++, d++) {
		send(sock, ((const char*)d) + 3, 1, 0);
		send(sock, ((const char*)d) + 2, 1, 0);
		send(sock, ((const char*)d) + 1, 1, 0);
		send(sock, ((const char*)d), 1, 0);
	}
}

void picoled::CloseSocket()
{
	int status = 0;
	if (VALID_SOCKET(sock)) {
#ifdef _WIN32
		status = shutdown(sock, SD_BOTH);
		if (status == 0) { status = closesocket(sock); }
#else
		status = shutdown(sock, SHUT_RDWR);
		if (status == 0) { status = close(sock); }
#endif
		sock = INVALID_SOCKET;
	}
}

void picoled::ExecuteCommand()
{
	int status = 0;
	if (!strncmp(text, "quit", 4) && text[4] == '\0') {
		if (VALID_SOCKET(sock)) CloseSocket();
		k3winObj::ExitLoop();
	} else if(!strncmp(text, "disconnect", 10) && text[10] == '\0') {
		if (VALID_SOCKET(sock)) {
			CloseSocket();
			printf("Disconnected\n");
		}
	} else if (!strncmp(text, "open", 4) && isspace(text[4])) {
		uint32_t start = 4;
		while (isspace(text[start])) start++;
		k3imageObj::ReformatFromFile(cpu_image, text + start, 0, 0, 0, k3fmt::B5G6R5_UNORM);
		gfx->WaitGpuIdle();
		k3resourceDesc rdesc;
		cpu_image->GetDesc(&rdesc);
		printf("opening \"%s\" width=%d height=%d\n", text + start, rdesc.width, rdesc.height);
		if (rdesc.width != image_width || rdesc.height != image_height) {
			k3viewDesc vdesc = { 0 };
			gpu_image = gfx->CreateSurface(&rdesc, &vdesc, &vdesc, NULL);
			if (gpu_image == NULL) {
				k3error::Handler("Could not create gpu image surface", "ExecuteCommand()");
				return;
			}
			image_width = rdesc.width;
			image_height = rdesc.height;
		}

		k3resource res;
		cmd->Reset();

		res = gpu_image->GetResource();
		cmd->TransitionResource(res, k3resourceState::COPY_DEST);
		cmd->UploadImage(cpu_image, res);
		cmd->TransitionResource(res, k3resourceState::SHADER_RESOURCE);

		cmd->Close();
		gfx->SubmitCmdBuf(cmd);
		gfx->WaitGpuIdle();

		num_back_anim = 1;
		back_anim[0].start_src_data = 0;
		back_anim[0].width = image_width;
		back_anim[0].pitch = (image_width + 1) / 2;
		back_anim[0].height = image_height;
		back_anim[0].start_x = 0;
		back_anim[0].start_y = 0;
		back_anim[0].delta_src_data = 0;
		back_anim[0].delta_x = 0;
		back_anim[0].num_frames_x = 0;
		back_anim[0].delta_y = 0;
		back_anim[0].num_frames_y = 0;
		back_anim[0].num_src_inc = 0;
		back_anim[0].num_frames = 60;
		back_anim[0].num_loops = 0;

	} else if (!strncmp(text, "hanim", 5) && isspace(text[5])) {
		uint32_t start = 5;
		while (isspace(text[start])) start++;
		uint32_t frames, period;
		sscanf(text + start, "%d%d", &frames, &period);
		if (frames == 0) frames = 1;
		if (period == 0) period = 60;
		back_anim[num_back_anim - 1].pitch = (image_width + 1) / 2;
		back_anim[num_back_anim - 1].delta_src_data = ((4 * back_anim[num_back_anim - 1].pitch) / frames) & ~0x3;
		back_anim[num_back_anim - 1].width = back_anim[num_back_anim - 1].delta_src_data / 2;
		back_anim[num_back_anim - 1].num_src_inc = frames;
		back_anim[num_back_anim - 1].num_frames = period;

	} else if (!strncmp(text, "vanim", 5) && isspace(text[5])) {
		uint32_t start = 5;
		while (isspace(text[start])) start++;
		uint32_t frames, period;
		sscanf(text + start, "%d%d", &frames, &period);
		if (frames == 0) frames = 1;
		if (period == 0) period = 60;
		back_anim[num_back_anim - 1].height = image_height;
		back_anim[num_back_anim - 1].delta_src_data = ((4 * back_anim[num_back_anim - 1].pitch * back_anim[num_back_anim - 1].height) / frames) & ~0x3;
		back_anim[num_back_anim - 1].height = (back_anim[num_back_anim - 1].height) / frames;
		back_anim[num_back_anim - 1].num_src_inc = frames;
		back_anim[num_back_anim - 1].num_frames = period;

	} else if (!strncmp(text, "hscroll", 7) && isspace(text[7])) {
		uint32_t start = 7;
		while (isspace(text[start])) start++;
		int32_t inc, period;
		sscanf(text + start, "%d%d", &inc, &period);
		if (period == 0) period = 60;
		back_anim[num_back_anim - 1].delta_x = inc;
		back_anim[num_back_anim - 1].num_frames_x = period;

	} else if (!strncmp(text, "vscroll", 7) && isspace(text[7])) {
		uint32_t start = 7;
		while (isspace(text[start])) start++;
		int32_t inc, period;
		sscanf(text + start, "%d%d", &inc, &period);
		if (period == 0) period = 60;
		back_anim[num_back_anim - 1].delta_y = inc;
		back_anim[num_back_anim - 1].num_frames_y = period;

	} else if (!strncmp(text, "period", 6) && isspace(text[6])) {
		uint32_t start = 6;
		while (isspace(text[start])) start++;
		uint32_t period = atoi(text + start);
		if (period == 0) period = 60;  // default to 1 second
		back_anim[num_back_anim - 1].num_loops = period;

	} else if (!strncmp(text, "connect", 7) && isspace(text[7])) {
		uint32_t start = 7;
		while (isspace(text[start])) start++;
		struct addrinfo* result = NULL;
		struct addrinfo* ptr = NULL;
		struct addrinfo hints;
		memset(&hints, 0x0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		if (getaddrinfo(text + start, PORT, &hints, &result) != 0) {
			k3error::Handler("Could not find server", "ExecuteCommand()");
			return;
		}
		printf("server: %hhu.%hhu.%hhu.%hhu:%d\n",
			result->ai_addr->sa_data[2],
			result->ai_addr->sa_data[3],
			result->ai_addr->sa_data[4],
			result->ai_addr->sa_data[5],
			result->ai_addr->sa_data[0] << 8 | (uint8_t)result->ai_addr->sa_data[1]);

		// in case a socket is already open, shut it down
		CloseSocket();

		// Attempt to connect to an address until one succeeds
		for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

			// Create a SOCKET for connecting to server
			sock = socket(ptr->ai_family, ptr->ai_socktype,
				ptr->ai_protocol);
			if (!VALID_SOCKET(sock)) {
				k3error::Handler("socket failure", "ExecuteCommand()");
				return;
			}

			// Connect to server.
			status = connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen);
			if (status == SOCKET_ERROR) {
				CloseSocket();
				continue;
			}
			break;
		}

		freeaddrinfo(result);

		if (!VALID_SOCKET(sock)) {
			k3error::Handler("Unable to connect to server", "ExecuteCommand()");
			return;
		}

#ifdef _WIN32
		uint32_t timeout = 5000;
#else
		struct timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;
#endif
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*) &timeout, sizeof(timeout)) < 0)
			k3error::Handler("setsockopt failed", "ExecuteCommand()");

		if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*) &timeout, sizeof timeout) < 0)
			k3error::Handler("setsockopt failed", "ExecuteCommand()");
		
		char buf[8] = "@b\n";
		status = send(sock, buf, 3, 0);
		if (status == SOCKET_ERROR) {
			k3error::Handler("Could not send to socket", "ExecuteCommand()");
			CloseSocket();
			return;
		}

		status = recv(sock, buf, 6, 0);
		if (status == SOCKET_ERROR) {
			k3error::Handler("Could not receive from socket", "ExecuteCommand()");
			CloseSocket();
			return;
		}
		buf[6] = '\0';
		printf("Message received: %s\n", buf);
	} else if (!strncmp(text, "upload", 6) && text[6] == '\0') {
		if (VALID_SOCKET(sock) && num_back_anim > 0) {
			// turn off lights while we load things up
			char buf[8] = "@B\0\n";
			char inbuf[8];
			printf("%s", buf);
			status = send(sock, buf, 4, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			status = recv(sock, inbuf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			inbuf[6] = '\0';
			printf("B: %s", inbuf);

			uint32_t v[2];
			v[0] = 0;
			v[1] = 2 * image_width * image_height;
			if (v[1] == 0) {
				k3error::Handler("No image loaded", "ExecuteCommand()");
				return;
			}
			buf[1] = 'D';
			status = send(sock, buf, 2, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			SendArray32(2, v);
			const uint16_t* image_data = (const uint16_t*)cpu_image->MapForRead();
			uint32_t i;
			for (i = 0; i < image_height; i++) {
				SendArray16(image_width, image_data);
				image_data += cpu_image->GetPitch() / 2;
			}
			cpu_image->Unmap();
			status = recv(sock, inbuf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			printf("D: %s", inbuf);

			buf[1] = 'A';
			buf[2] = num_back_anim;
			status = send(sock, buf, 3, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			for (i = 0; i < num_back_anim; i++) {
				anim_t* a = &back_anim[i];
				SendArray32(1, &(a->start_src_data));
				SendArray16(5, (uint16_t*)&(a->width));
				SendArray32(1, (uint32_t*)&(a->delta_src_data));
				SendArray16(7, (uint16_t*)&(a->delta_x));
				status = recv(sock, inbuf, 6, 0);
				if (status == SOCKET_ERROR) {
					k3error::Handler("Could not receive from socket", "ExecuteCommand()");
					CloseSocket();
					return;
				}
				printf("A: %s", inbuf);
			}

			buf[1] = 'B';
			buf[2] = 8;
			status = send(sock, buf, 4, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			status = recv(sock, inbuf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			inbuf[6] = '\0';
			printf("B: %s", inbuf);


		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	} else if (!strncmp(text, "brightness", 10) && isspace(text[10])) {
		if (VALID_SOCKET(sock)) {
			uint32_t start = 10;
			while (isspace(text[start])) start++;
			char buf[8] = "@B\0\n";
			buf[2] = atoi(text + start);

			status = send(sock, buf, 4, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}

			status = recv(sock, buf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			buf[6] = '\0';
			printf("Message received: %s\n", buf);
		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	} else if (!strncmp(text, "overlay", 7) && isspace(text[7])) {
		if (VALID_SOCKET(sock)) {
			uint32_t start = 7;
			while (isspace(text[start])) start++;
			char buf[8] = "@V\0\n";
			buf[2] = atoi(text + start);

			status = send(sock, buf, 4, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}

			status = recv(sock, buf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			buf[6] = '\0';
			printf("Message received: %s\n", buf);
		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	} else if (!strncmp(text, "save", 4) && isspace(text[4])) {
		if (VALID_SOCKET(sock)) {
			char buf[8] = "@W\n";

			status = send(sock, buf, 3, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}

			status = recv(sock, buf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			buf[6] = '\0';
			printf("Message received: %s\n", buf);
		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	} else if (!strncmp(text, "load", 4) && isspace(text[4])) {
		if (VALID_SOCKET(sock)) {
			char buf[8] = "@R\n";

			status = send(sock, buf, 3, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}

			status = recv(sock, buf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			buf[6] = '\0';
			printf("Message received: %s\n", buf);
		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	} else if (!strncmp(text, "erase", 5) && isspace(text[5])) {
		if (VALID_SOCKET(sock)) {
			char buf[8] = "@E\n";

			status = send(sock, buf, 3, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}

			status = recv(sock, buf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			buf[6] = '\0';
			printf("Message received: %s\n", buf);
		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	} else if (!strncmp(text, "unerase", 7) && isspace(text[7])) {
		if (VALID_SOCKET(sock)) {
			char buf[8] = "@U\n";

			status = send(sock, buf, 3, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not send to socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}

			status = recv(sock, buf, 6, 0);
			if (status == SOCKET_ERROR) {
				k3error::Handler("Could not receive from socket", "ExecuteCommand()");
				CloseSocket();
				return;
			}
			buf[6] = '\0';
			printf("Message received: %s\n", buf);
		} else {
			k3error::Handler("No open socket", "ExecuteCommand()");
		}
	}
}

void K3CALLBACK picoled::DisplayCallback(void* ptr)
{
	picoled* pl = (picoled*)ptr;
	pl->Display();
}

void K3CALLBACK picoled::KeyboardCallback(void* ptr, k3key k, char c, k3keyState state)
{
	picoled* pl = (picoled*)ptr;
	pl->Keyboard(k, c, state);
}
