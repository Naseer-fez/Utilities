import requests
import json
from typing import List, Dict, Any, Optional
from backend.config import OLLAMA_URL, OLLAMA_DEFAULT_MODEL, GEMINI_API_KEY, OPENAI_API_KEY, ANTHROPIC_API_KEY

class LLMClient:
    """Unified client for invoking local Ollama models and cloud-based models."""

    @staticmethod
    def list_local_models() -> List[str]:
        """Queries the local Ollama instance to list all downloaded/pulled models."""
        try:
            response = requests.get(f"{OLLAMA_URL}/api/tags", timeout=3)
            if response.status_code == 200:
                data = response.json()
                models = [model["name"] for model in data.get("models", [])]
                # Filter out embedding models as they cannot be used for text generation
                models = [m for m in models if "embed" not in m.lower()]
                return models
        except Exception:
            # Return empty if Ollama is offline
            pass
        return []

    @classmethod
    def check_ollama_status(cls) -> Dict[str, Any]:
        """Checks if local Ollama is active and returns status info."""
        models = cls.list_local_models()
        if models:
            return {"status": "online", "models": models}
        return {"status": "offline", "models": []}

    @staticmethod
    def _generate_ollama(model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7) -> str:
        """Sends generation prompt to local Ollama API."""
        try:
            url = f"{OLLAMA_URL}/api/generate"
            payload = {
                "model": model,
                "prompt": prompt,
                "stream": False,
                "options": {
                    "temperature": temperature
                }
            }
            if system_prompt:
                payload["system"] = system_prompt

            response = requests.post(url, json=payload, timeout=120)
            if response.status_code == 200:
                return response.json().get("response", "").strip()
            else:
                return f"Error: Ollama returned status code {response.status_code}: {response.text}"
        except Exception as e:
            return f"Error: Failed to connect to Ollama model '{model}' at {OLLAMA_URL}. Details: {e}"

    @staticmethod
    def _generate_gemini(model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7) -> str:
        """Invokes Google Gemini API via direct HTTP request."""
        if not GEMINI_API_KEY:
            return "Error: Gemini API key is missing. Please add it to your .env file."

        try:
            # Map clean name if needed (e.g. "gemini-1.5-flash")
            api_model = model if "/" in model else f"models/{model}"
            url = f"https://generativelanguage.googleapis.com/v1beta/{api_model}:generateContent?key={GEMINI_API_KEY}"
            
            contents = []
            if system_prompt:
                # Include system instruction if supported
                payload = {
                    "contents": [{"parts": [{"text": prompt}]}],
                    "systemInstruction": {"parts": [{"text": system_prompt}]},
                    "generationConfig": {"temperature": temperature}
                }
            else:
                payload = {
                    "contents": [{"parts": [{"text": prompt}]}],
                    "generationConfig": {"temperature": temperature}
                }

            response = requests.post(url, json=payload, timeout=30)
            if response.status_code == 200:
                data = response.json()
                try:
                    return data["candidates"][0]["content"]["parts"][0]["text"].strip()
                except (KeyError, IndexError):
                    return f"Error parsing Gemini response. Response body: {json.dumps(data)}"
            else:
                return f"Error: Gemini API returned status code {response.status_code}: {response.text}"
        except Exception as e:
            return f"Error: Failed to reach Gemini API. Details: {e}"

    @staticmethod
    def _generate_openai(model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7) -> str:
        """Invokes OpenAI Chat Completions API via direct HTTP request."""
        if not OPENAI_API_KEY:
            return "Error: OpenAI API key is missing. Please add it to your .env file."

        try:
            url = "https://api.openai.com/v1/chat/completions"
            headers = {
                "Content-Type": "application/json",
                "Authorization": f"Bearer {OPENAI_API_KEY}"
            }
            
            messages = []
            if system_prompt:
                messages.append({"role": "system", "content": system_prompt})
            messages.append({"role": "user", "content": prompt})

            payload = {
                "model": model,
                "messages": messages,
                "temperature": temperature
            }

            response = requests.post(url, json=payload, headers=headers, timeout=30)
            if response.status_code == 200:
                data = response.json()
                return data["choices"][0]["message"]["content"].strip()
            else:
                return f"Error: OpenAI API returned status code {response.status_code}: {response.text}"
        except Exception as e:
            return f"Error: Failed to reach OpenAI API. Details: {e}"

    @staticmethod
    def _generate_anthropic(model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7) -> str:
        """Invokes Anthropic Claude API via direct HTTP request."""
        if not ANTHROPIC_API_KEY:
            return "Error: Anthropic API key is missing. Please add it to your .env file."

        try:
            url = "https://api.anthropic.com/v1/messages"
            headers = {
                "content-type": "application/json",
                "x-api-key": ANTHROPIC_API_KEY,
                "anthropic-version": "2023-06-01"
            }

            payload = {
                "model": model,
                "max_tokens": 4000,
                "messages": [{"role": "user", "content": prompt}],
                "temperature": temperature
            }
            if system_prompt:
                payload["system"] = system_prompt

            response = requests.post(url, json=payload, headers=headers, timeout=30)
            if response.status_code == 200:
                data = response.json()
                try:
                    return data["content"][0]["text"].strip()
                except (KeyError, IndexError):
                    return f"Error parsing Anthropic response: {json.dumps(data)}"
            else:
                return f"Error: Anthropic API returned status code {response.status_code}: {response.text}"
        except Exception as e:
            return f"Error: Failed to reach Anthropic API. Details: {e}"

    @staticmethod
    def _generate_studylm(model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7) -> str:
        """Invokes our local scratch-built StudyLM engine via optimized subprocess execution."""
        import subprocess
        import sys
        import os
        import re
        
        # Determine current python executable path
        python_exe = sys.executable if sys.executable else "python"
        
        # Build the payload JSON
        payload = {
            "task": "", # Will be dynamically inferred by the engine based on prompt structure
            "query": prompt,
            "context": "", # In synthesizer/deck generation, prompt/system_prompt will contain context
            "system": system_prompt or "",
            "temperature": temperature
        }
        
        # Extract RAG context from prompt if present
        context_matches = re.findall(
            r"(?:STUDY MATERIALS CONTEXT|SOURCE CONTEXT|STUDY DOCUMENTS CONTEXT):\n?(.*?)(?:\n\n|\Z)", 
            prompt, 
            re.DOTALL | re.IGNORECASE
        )
        if context_matches:
            payload["context"] = context_matches[0].strip()
            # Clean context out of query to keep query sharp and focused
            payload["query"] = prompt.replace(context_matches[0], "").replace("STUDY MATERIALS CONTEXT:", "").replace("SOURCE CONTEXT:", "").replace("STUDY DOCUMENTS CONTEXT:", "").strip()
            
        # Extract draft answer from prompt if present (critic flow)
        draft_matches = re.findall(
            r"DRAFT ANSWER:\n?(.*?)(?:\n\n|\Z)", 
            prompt, 
            re.DOTALL | re.IGNORECASE
        )
        if draft_matches:
            payload["draft"] = draft_matches[0].strip()
            
        try:
            # Execute backend/local_llm.py, writing input payload to stdin
            script_path = os.path.join(os.path.dirname(__file__), "local_llm.py")
            process = subprocess.Popen(
                [python_exe, script_path],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8"
            )
            stdout, stderr = process.communicate(input=json.dumps(payload), timeout=10)
            if process.returncode == 0:
                return stdout.strip()
            else:
                return f"Error executing local StudyLM: {stderr.strip()}"
        except Exception as e:
            return f"Error: Failed to launch local StudyLM. Details: {e}"

    @classmethod
    def generate(cls, provider: str, model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7) -> str:
        """
        Primary generation router.
        
        Args:
            provider: 'ollama', 'gemini', 'openai', 'anthropic', or 'studylm'
            model: Name of the target model (e.g. 'llama3', 'gemini-1.5-flash', 'gpt-4o', 'studylm-1.0-scratch')
            prompt: Text prompt for generation
            system_prompt: Optional guide/instructions for model persona
            temperature: Creativity control (0.0 to 1.0)
        """
        provider = provider.lower().strip()
        
        if provider == "ollama":
            return cls._generate_ollama(model, prompt, system_prompt, temperature)
        elif provider == "gemini":
            return cls._generate_gemini(model, prompt, system_prompt, temperature)
        elif provider == "openai":
            return cls._generate_openai(model, prompt, system_prompt, temperature)
        elif provider == "anthropic":
            return cls._generate_anthropic(model, prompt, system_prompt, temperature)
        elif provider == "studylm":
            return cls._generate_studylm(model, prompt, system_prompt, temperature)
        else:
            return f"Error: Unsupported model provider '{provider}'."

    @classmethod
    def generate_stream(cls, provider: str, model: str, prompt: str, system_prompt: Optional[str] = None, temperature: float = 0.7):
        """
        Streams token chunks from the selected model provider.
        """
        provider = provider.lower().strip()
        
        if provider == "ollama":
            try:
                url = f"{OLLAMA_URL}/api/generate"
                payload = {
                    "model": model,
                    "prompt": prompt,
                    "stream": True,
                    "options": {
                        "temperature": temperature
                    }
                }
                if system_prompt:
                    payload["system"] = system_prompt

                response = requests.post(url, json=payload, stream=True, timeout=120)
                if response.status_code == 200:
                    for line in response.iter_lines():
                        if line:
                            chunk = json.loads(line.decode('utf-8'))
                            response_text = chunk.get("response", "")
                            if response_text:
                                yield response_text
                else:
                    yield f"Error: Ollama returned status code {response.status_code}"
            except Exception as e:
                yield f"Error connecting to Ollama: {e}"

        elif provider == "gemini":
            if not GEMINI_API_KEY:
                yield "Error: Gemini API key is missing."
                return
            try:
                api_model = model if "/" in model else f"models/{model}"
                url = f"https://generativelanguage.googleapis.com/v1beta/{api_model}:streamGenerateContent?key={GEMINI_API_KEY}"
                
                if system_prompt:
                    payload = {
                        "contents": [{"parts": [{"text": prompt}]}],
                        "systemInstruction": {"parts": [{"text": system_prompt}]},
                        "generationConfig": {"temperature": temperature}
                    }
                else:
                    payload = {
                        "contents": [{"parts": [{"text": prompt}]}],
                        "generationConfig": {"temperature": temperature}
                    }

                response = requests.post(url, json=payload, stream=True, timeout=30)
                if response.status_code == 200:
                    import re
                    for line in response.iter_lines():
                        if line:
                            line_decoded = line.decode('utf-8').strip()
                            cleaned = line_decoded.lstrip(',[').rstrip(',]')
                            if cleaned:
                                try:
                                    chunk_data = json.loads(cleaned)
                                    text = chunk_data["candidates"][0]["content"]["parts"][0]["text"]
                                    yield text
                                except Exception:
                                    m = re.search(r'"text":\s*"((?:[^"\\]|\\.)*)"', cleaned)
                                    if m:
                                        try:
                                            text = json.loads(f'"{m.group(1)}"')
                                            yield text
                                        except:
                                            yield m.group(1)
                else:
                    yield f"Error: Gemini API returned status code {response.status_code}"
            except Exception as e:
                yield f"Error connecting to Gemini: {e}"

        elif provider == "openai":
            if not OPENAI_API_KEY:
                yield "Error: OpenAI API key is missing."
                return
            try:
                url = "https://api.openai.com/v1/chat/completions"
                headers = {
                    "Content-Type": "application/json",
                    "Authorization": f"Bearer {OPENAI_API_KEY}"
                }
                
                messages = []
                if system_prompt:
                    messages.append({"role": "system", "content": system_prompt})
                messages.append({"role": "user", "content": prompt})

                payload = {
                    "model": model,
                    "messages": messages,
                    "temperature": temperature,
                    "stream": True
                }

                response = requests.post(url, json=payload, headers=headers, stream=True, timeout=30)
                if response.status_code == 200:
                    for line in response.iter_lines():
                        if line:
                            line_decoded = line.decode('utf-8').strip()
                            if line_decoded.startswith("data: "):
                                data_str = line_decoded[6:]
                                if data_str == "[DONE]":
                                    break
                                try:
                                    chunk_data = json.loads(data_str)
                                    delta = chunk_data["choices"][0]["delta"]
                                    if "content" in delta:
                                        yield delta["content"]
                                except Exception:
                                    pass
                else:
                    yield f"Error: OpenAI API returned status code {response.status_code}"
            except Exception as e:
                yield f"Error connecting to OpenAI: {e}"

        elif provider == "anthropic":
            if not ANTHROPIC_API_KEY:
                yield "Error: Anthropic API key is missing."
                return
            try:
                url = "https://api.anthropic.com/v1/messages"
                headers = {
                    "content-type": "application/json",
                    "x-api-key": ANTHROPIC_API_KEY,
                    "anthropic-version": "2023-06-01"
                }

                payload = {
                    "model": model,
                    "max_tokens": 4000,
                    "messages": [{"role": "user", "content": prompt}],
                    "temperature": temperature,
                    "stream": True
                }
                if system_prompt:
                    payload["system"] = system_prompt

                response = requests.post(url, json=payload, headers=headers, stream=True, timeout=30)
                if response.status_code == 200:
                    for line in response.iter_lines():
                        if line:
                            line_decoded = line.decode('utf-8').strip()
                            if line_decoded.startswith("data: "):
                                data_str = line_decoded[6:]
                                try:
                                    chunk_data = json.loads(data_str)
                                    if chunk_data.get("type") == "content_block_delta":
                                        text_delta = chunk_data.get("delta", {}).get("text", "")
                                        if text_delta:
                                            yield text_delta
                                except Exception:
                                    pass
                else:
                    yield f"Error: Anthropic API returned status code {response.status_code}"
            except Exception as e:
                yield f"Error connecting to Anthropic: {e}"

        elif provider == "studylm":
            try:
                import time
                result = cls._generate_studylm(model, prompt, system_prompt, temperature)
                words = result.split(" ")
                for i, word in enumerate(words):
                    yield (word + " " if i < len(words) - 1 else word)
                    time.sleep(0.01)
            except Exception as e:
                yield f"Error generating StudyLM response: {e}"
        else:
            yield f"Error: Unsupported provider '{provider}'."

