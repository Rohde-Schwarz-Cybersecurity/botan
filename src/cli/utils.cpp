/*
* (C) 2009,2010,2014,2015 Jack Lloyd
* (C) 2017 René Korthaus, Rohde & Schwarz Cybersecurity
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#include "cli.h"

#include <botan/version.h>
#include <botan/hash.h>
#include <botan/mac.h>
#include <botan/rng.h>
#include <botan/cpuid.h>
#include <botan/hex.h>
#include <botan/parsing.h>
#include <botan/internal/stl_util.h>
#include <sstream>
#include <iostream>
#include <iterator>
#include <iomanip>

#if defined(BOTAN_HAS_BASE64_CODEC)
   #include <botan/base64.h>
#endif

#if defined(BOTAN_HAS_HTTP_UTIL)
   #include <botan/http_util.h>
#endif

#if defined(BOTAN_HAS_BCRYPT)
   #include <botan/bcrypt.h>
#endif

namespace Botan_CLI {

class Print_Help final : public Command
   {
   public:
      Print_Help() : Command("help") {}

      std::string help_text() const override
         {
         const std::set<std::string> avail_commands =
            Botan::map_keys_as_set(Botan_CLI::Command::global_registry());

         const std::map<std::string, std::string> groups_description
#if defined(BOTAN_HAS_AES) && defined(BOTAN_HAS_AEAD_MODES)
            { { "encryption", "Encryption" },
#endif
#if defined(BOTAN_HAS_COMPRESSION)
            { "compression", "Compression" },
#endif
            { "hash", "Hash Functions" },
#if defined(BOTAN_HAS_HMAC)
            { "hmac", "HMAC" },
#endif
            { "numtheory", "Number Theory" },
#if defined(BOTAN_HAS_BCRYPT)
            { "passhash", "Password Hashing" },
#endif
            { "pubkey", "Public Key Cryptography" },
#if defined(BOTAN_HAS_TLS)
            { "tls", "TLS" },
#endif
#if defined(BOTAN_HAS_X509_CERTIFICATES)
            { "x509", "X.509" },
#endif
            { "misc", "Miscellaneous" }
         };
   
      const std::set<std::string> groups =
         Botan::map_keys_as_set(groups_description);

      std::ostringstream oss;

      oss << "Usage: botan <cmd> <cmd-options>\n\n";
      oss << "All commands support --verbose --help --output= --error-output= --rng-type= --drbg-seed=\n\n";
      oss << "Available commands:\n\n";

      for(auto& cmd_group : groups)
         {
         oss << groups_description.at(cmd_group) << ":\n";
         for(auto& cmd_name : avail_commands)
            {
            auto cmd = Botan_CLI::Command::get_cmd(cmd_name);
            if(cmd->group() == cmd_group)
               {
               oss << "   " << std::setw(16) << std::left << cmd->cmd_name() << "   " << cmd->short_description() << "\n";
               }
            }
         oss << "\n";
         }

      return oss.str();
      }

      std::string group() const override
         {
         return "";
         }

      std::string short_description() const override
         {
         return "Prints a help string";
         }

      std::string long_description() const override
         {
         return "Prints a help string";
         }

      void go() override
         {
         this->set_return_code(1);
         output() << help_text();
         }
   };

BOTAN_REGISTER_COMMAND("help", Print_Help);

class Config_Info final : public Command
   {
   public:
      Config_Info() : Command("config info_type") {}

      std::string help_text() const override
         {
         return "Usage: config info_type\n"
                "   prefix: Print install prefix\n"
                "   cflags: Print include params\n"
                "   ldflags: Print linker params\n"
                "   libs: Print libraries\n";
         }

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Print the used prefix, cflags, ldflags or libs";
         }

      std::string long_description() const override
         {
         // TODO use help_text()
         return help_text();
         }

      void go() override
         {
         const std::string arg = get_arg("info_type");

         if(arg == "prefix")
            {
            output() << BOTAN_INSTALL_PREFIX << "\n";
            }
         else if(arg == "cflags")
            {
            output() << "-I" << BOTAN_INSTALL_PREFIX << "/" << BOTAN_INSTALL_HEADER_DIR << "\n";
            }
         else if(arg == "ldflags")
            {
            if(*BOTAN_LINK_FLAGS)
               output() << BOTAN_LINK_FLAGS << ' ';
            output() << "-L" << BOTAN_INSTALL_PREFIX << "/" << BOTAN_INSTALL_LIB_DIR << "\n";
            }
         else if(arg == "libs")
            {
            output() << "-lbotan-" << Botan::version_major() << " " << BOTAN_LIB_LINK << "\n";
            }
         else
            {
            throw CLI_Usage_Error("Unknown option to botan config " + arg);
            }
         }
   };

BOTAN_REGISTER_COMMAND("config", Config_Info);

class Version_Info final : public Command
   {
   public:
      Version_Info() : Command("version --full") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Print version info";
         }

      std::string long_description() const override
         {
         return "Print version. Pass --full for additional details.";
         }

      void go() override
         {
         if(flag_set("full"))
            {
            output() << Botan::version_string() << "\n";
            }
         else
            {
            output() << Botan::short_version_string() << "\n";
            }
         }
   };

BOTAN_REGISTER_COMMAND("version", Version_Info);

class Print_Cpuid final : public Command
   {
   public:
      Print_Cpuid() : Command("cpuid") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "List available processor flags (aes_ni, SIMD extensions, ...)";
         }

      std::string long_description() const override
         {
         return short_description();
         }

      void go() override
         {
         output() << "CPUID flags: " << Botan::CPUID::to_string() << "\n";
         }
   };

BOTAN_REGISTER_COMMAND("cpuid", Print_Cpuid);

class Hash final : public Command
   {
   public:
      Hash() : Command("hash --algo=SHA-256 --buf-size=4096 *files") {}

      std::string group() const override
         {
         return "hash";
         }

      std::string short_description() const override
         {
         return "Compute the message digest of given file(s)";
         }

      std::string long_description() const override
         {
         return "Compute the *algo* digest over the data in *files*. "
               "*files* defaults to STDIN.";
         }

      void go() override
         {
         const std::string hash_algo = get_arg("algo");
         std::unique_ptr<Botan::HashFunction> hash_fn(Botan::HashFunction::create(hash_algo));

         if(!hash_fn)
            {
            throw CLI_Error_Unsupported("hashing", hash_algo);
            }

         const size_t buf_size = get_arg_sz("buf-size");

         std::vector<std::string> files = get_arg_list("files");
         if(files.empty())
            {
            files.push_back("-");
            } // read stdin if no arguments on command line

         for(const std::string& fsname : files)
            {
            try
               {
               auto update_hash = [&](const uint8_t b[], size_t l) { hash_fn->update(b, l); };
               read_file(fsname, update_hash, buf_size);
               output() << Botan::hex_encode(hash_fn->final()) << " " << fsname << "\n";
               }
            catch(CLI_IO_Error& e)
               {
               error_output() << e.what() << "\n";
               }
            }
         }
   };

BOTAN_REGISTER_COMMAND("hash", Hash);

class RNG final : public Command
   {
   public:
      RNG() : Command("rng --system --rdrand --auto --entropy --drbg --drbg-seed= *bytes") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Sample random bytes from the specified rng";
         }

      std::string long_description() const override
         {
         return "Sample *bytes* random bytes from the specified random number generator. "
               "If system is set, the Botan System_RNG is used. "
               "If system is unset and rdrand is set, the hardware rng RDRAND_RNG is used. "
               "If both are unset, the Botan AutoSeeded_RNG is used.";
         }

      void go() override
         {
         std::string type = get_arg("rng-type");

         if(type.empty())
            {
            for(std::string flag : { "system", "rdrand", "auto", "entropy", "drbg" })
               {
               if(flag_set(flag))
                  {
                  type = flag;
                  break;
                  }
               }
            }

         const std::string drbg_seed = get_arg("drbg-seed");
         std::unique_ptr<Botan::RandomNumberGenerator> rng = cli_make_rng(type, drbg_seed);

         for(const std::string& req : get_arg_list("bytes"))
            {
            output() << Botan::hex_encode(rng->random_vec(Botan::to_u32bit(req))) << "\n";
            }
         }
   };

BOTAN_REGISTER_COMMAND("rng", RNG);

#if defined(BOTAN_HAS_HTTP_UTIL)

class HTTP_Get final : public Command
   {
   public:
      HTTP_Get() : Command("http_get --redirects=1 --timeout=3000 url") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Retrieve resource from the passed http/https url";
         }

      std::string long_description() const override
         {
         return short_description();
         }

      void go() override
         {
         const std::string url = get_arg("url");
         const std::chrono::milliseconds timeout(get_arg_sz("timeout"));
         const size_t redirects = get_arg_sz("redirects");

         output() << Botan::HTTP::GET_sync(url, redirects, timeout) << "\n";
         }
   };

BOTAN_REGISTER_COMMAND("http_get", HTTP_Get);

#endif // http_util

#if defined(BOTAN_HAS_HEX_CODEC)

class Hex_Encode final : public Command
   {
   public:
      Hex_Encode() : Command("hex_enc file") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Hex encode a given file";
         }

      std::string long_description() const override
         {
         return short_description();
         }

      void go() override
         {
         auto hex_enc_f = [&](const uint8_t b[], size_t l) { output() << Botan::hex_encode(b, l); };
         this->read_file(get_arg("file"), hex_enc_f, 2);
         }
   };

BOTAN_REGISTER_COMMAND("hex_enc", Hex_Encode);

class Hex_Decode final : public Command
   {
   public:
      Hex_Decode() : Command("hex_dec file") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Hex decode a given file";
         }

      std::string long_description() const override
         {
         return short_description();
         }

      void go() override
         {
         auto hex_dec_f = [&](const uint8_t b[], size_t l)
            {
            std::vector<uint8_t> bin = Botan::hex_decode(reinterpret_cast<const char*>(b), l);
            output().write(reinterpret_cast<const char*>(bin.data()), bin.size());
            };

         this->read_file(get_arg("file"), hex_dec_f, 2);
         }
   };

BOTAN_REGISTER_COMMAND("hex_dec", Hex_Decode);

#endif

#if defined(BOTAN_HAS_BASE64_CODEC)

class Base64_Encode final : public Command
   {
   public:
      Base64_Encode() : Command("base64_enc file") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Encode given file to Base64";
         }

      std::string long_description() const override
         {
         return short_description();
         }

      void go() override
         {
         auto onData = [&](const uint8_t b[], size_t l)
            {
            output() << Botan::base64_encode(b, l);
            };
         this->read_file(get_arg("file"), onData, 768);
         }
   };

BOTAN_REGISTER_COMMAND("base64_enc", Base64_Encode);

class Base64_Decode final : public Command
   {
   public:
      Base64_Decode() : Command("base64_dec file") {}

      std::string group() const override
         {
         return "misc";
         }

      std::string short_description() const override
         {
         return "Decode Base64 encoded file";
         }

      std::string long_description() const override
         {
         return short_description();
         }

      void go() override
         {
         auto write_bin = [&](const uint8_t b[], size_t l)
            {
            Botan::secure_vector<uint8_t> bin = Botan::base64_decode(reinterpret_cast<const char*>(b), l);
            output().write(reinterpret_cast<const char*>(bin.data()), bin.size());
            };

         this->read_file(get_arg("file"), write_bin, 1024);
         }
   };

BOTAN_REGISTER_COMMAND("base64_dec", Base64_Decode);

#endif // base64

#if defined(BOTAN_HAS_BCRYPT)

class Generate_Bcrypt final : public Command
   {
   public:
      Generate_Bcrypt() : Command("gen_bcrypt --work-factor=12 password") {}

      std::string group() const override
         {
         return "passhash";
         }

      std::string short_description() const override
         {
         return "Calculate the bcrypt password digest of a given file";
         }

      std::string long_description() const override
         {
         return "Calculate the bcrypt password digest of file. "
               "work-factor is an integer between 1 and 18. "
               "A higher work-factor value results in a more expensive hash calculation.";
         }

      void go() override
         {
         const std::string password = get_arg("password");
         const size_t wf = get_arg_sz("work-factor");

         if(wf < 4 || wf > 18)
            {
            error_output() << "Invalid bcrypt work factor\n";
            }
         else
            {
            const uint16_t wf16 = static_cast<uint16_t>(wf);
            output() << Botan::generate_bcrypt(password, rng(), wf16) << "\n";
            }
         }
   };

BOTAN_REGISTER_COMMAND("gen_bcrypt", Generate_Bcrypt);

class Check_Bcrypt final : public Command
   {
   public:
      Check_Bcrypt() : Command("check_bcrypt password hash") {}

      std::string group() const override
         {
         return "passhash";
         }

      std::string short_description() const override
         {
         return "Checks a given bcrypt hash against hash";
         }

      std::string long_description() const override
         {
         return "Checks if the bcrypt hash of the passed *password* equals the passed *hash* value.";
         }

      void go() override
         {
         const std::string password = get_arg("password");
         const std::string hash = get_arg("hash");

         if(hash.length() != 60)
            {
            error_output() << "Note: bcrypt '" << hash << "' has wrong length and cannot be valid\n";
            }

         const bool ok = Botan::check_bcrypt(password, hash);

         output() << "Password is " << (ok ? "valid" : "NOT valid") << std::endl;
         }
   };

BOTAN_REGISTER_COMMAND("check_bcrypt", Check_Bcrypt);

#endif // bcrypt

#if defined(BOTAN_HAS_HMAC)

class HMAC final : public Command
   {
   public:
      HMAC() : Command("hmac --hash=SHA-256 --buf-size=4096 key *files") {}

      std::string group() const override
         {
         return "hmac";
         }

      std::string short_description() const override
         {
         return "Compute the HMAC tag of given file(s)";
         }

      std::string long_description() const override
         {
         return "Compute the HMAC tag with the cryptographic hash function "
               "*hash* using the key in file key over the data in *files*. "
               "*files* defaults to STDIN.";
         }

      void go() override
         {
         const std::string hash_algo = get_arg("hash");
         std::unique_ptr<Botan::MessageAuthenticationCode> hmac =
            Botan::MessageAuthenticationCode::create("HMAC(" + hash_algo + ")");

         if(!hmac)
            { throw CLI_Error_Unsupported("HMAC", hash_algo); }

         hmac->set_key(slurp_file(get_arg("key")));

         const size_t buf_size = get_arg_sz("buf-size");

         std::vector<std::string> files = get_arg_list("files");
         if(files.empty())
            { files.push_back("-"); } // read stdin if no arguments on command line

         for(const std::string& fsname : files)
            {
            try
               {
               auto update_hmac = [&](const uint8_t b[], size_t l) { hmac->update(b, l); };
               read_file(fsname, update_hmac, buf_size);
               output() << Botan::hex_encode(hmac->final()) << " " << fsname << "\n";
               }
            catch(CLI_IO_Error& e)
               {
               error_output() << e.what() << "\n";
               }
            }
         }
   };

BOTAN_REGISTER_COMMAND("hmac", HMAC);

#endif // hmac

}
