const fs = require('fs');

const clapPrefix = __dirname + "/modules/clap/include/clap/";

let code = "";
let types = {};

function addDirect(type, compatible) {
	types[type] = {
		type: 'direct',
		compatible: compatible,
	};
}
function addFile(path) {
	let cpp = fs.readFileSync(clapPrefix + path, 'utf-8');
	let parts = cpp.split('\ntypedef struct ');
	parts.slice(1).forEach(cpp => {
		let name = cpp.split(' ', 1)[0] + "_t";
		if (types[name]) return; // already defined (probably custom)
		let struct = cpp.substr(name.length).split("} " + name + ";", 1)[0];
		struct = struct.replace(/\/\/[^\n]*/g, '');
		let fields = struct.split(';').map(x => x.trim()).filter(x => x.length);

		let structType = types[name] = {
			type: 'struct',
			compatible: true,
			fields: []
		};
		fields.forEach(cpp => {
			let match;
			if (match = cpp.match(/^(.*) ([a-zA-Z_]+)$/)) {
				let fType = match[1], fName = match[2];
				if (!types[fType]) {
					console.error(cpp);
					throw Error("Unknown type in struct " + name + ": " + fType);
				}
				structType.fields.push({type: fType, name: fName});
			} else if (match = cpp.match(/^(.*)\s*\(CLAP_ABI \*([a-zA-Z_]+)\)\((.*)\)$/)) {
				let fReturn = match[1], fName = match[2], fArgs = match[3].split(',').map(x => x.trim()).filter(x => x != 'void');

				fArgs = fArgs.map(arg => {
					// They all should end with an argument name, so we strip this out
					arg = arg.replace(/[a-zA-Z_]+$/, '');
					if (!types[arg]) {
						console.error(cpp);
						console.error(code);
						throw Error("Unknown argument type in struct " + name + ": " + arg);
					}
					return arg;
				});

				if (fReturn == "void") {
					fReturn = null;
				}
				console.log(fName + ": (" + fArgs + ") -> " + fReturn);
				if (fReturn && !types[fReturn]) {
					console.error(cpp);
					throw Error("Unknown return type in struct " + name + ": " + fReturn);
				}

				let fnType = types[name + "." + fName] = {
					type: 'function',
					compatible: false,
					returnType: fReturn,
					argTypes: fArgs
				};
				structType.fields.push({
					type: name + "." + fName,
					name: fName,
				});
			} else {
				console.error(cpp);
				throw Error("Couldn't parse struct field in " + name);
			}
		});
		structType.fields.forEach(f => {
			if (!types[f.type].compatible) {
				structType.compatible = false;
			}
		});
		console.log(name, structType);
	});
}

addDirect("uint32_t", true);
addDirect("uint64_t", true);
addDirect("float32_t", true);
addDirect("float64_t", true);
addDirect("bool", true);
addDirect("const char *", false);

addDirect("clap_plugin_entry_t", false)

addFile("version.h");
addFile("entry.h");


let hCode = fs.readFileSync(__dirname + "/source/translate-clap-api.h", 'utf-8');
hCode = hCode.replace(/(\/\*translate-clap-api.js\*\/)((.|\n|\r)*)\/\*translate-clap-api.js\*\//, `/*translate-clap-api.js*/
${code}
	/*translate-clap-api.js*/`);
fs.writeFileSync(__dirname + "/source/translate-clap-api.h", hCode);
