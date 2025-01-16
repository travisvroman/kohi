/**
 * This script is used to upgrade old-style Kohi material
 * files to the new KSON-based format. It requires node to
 * run. The cloned repository already has this done, but
 * this file is left here for folks that have thier own 
 * material files to convert.
 * 
 * This should be run directly in the folder containing .kmt
 * files to be converted.
 * 
 * Note that by default this script renames the previous
 * file with an .old extension and writes a new kmt file
 * in the upgraded format.
 */
const fs = require('fs');

// NOTE: flip this to true to delete the .kmt.old files.
const deleteOldFiles = false;

function upgrade_file(filename) {
    const fileContent = fs.readFileSync(filename, 'utf-8');
    let lines = fileContent.split(/\r?\n/);
    if (lines[0].trim() == "version = 3") {
        console.log("File already upgraded. Nothing to do.", filename);
        return;
    }
    let inMap = false;
    let inProp = false;

    let currentMap = {};
    let currentProp = {};

    let material = {
        type: "pbr", // undefined materials will default to pbr
        version: 3,
        shader: undefined,
        maps: [],
        props: []
    };

    for (let i = 0; i < lines.length; ++i) {
        if (i == 0) {
            continue;
        }
        const line = lines[i].trim();

        if (line.length < 1) {
            continue;
        } else if (line[0] == "#") {
            continue;
        } else if (line[0] == '[') {
            if (line.toLowerCase() == "[map]") {
                inMap = true;
            } else if (line.toLowerCase() == "[/map]") {
                inMap = false;
                material.maps.push(currentMap);
                currentMap = {};
            } else if (line.toLowerCase() == "[prop]") {
                inProp = true;
            } else if (line.toLowerCase() == "[/prop]") {
                inProp = false;
                if (material.props.find(p => p.name == currentProp.name)) {
                    console.warn("Duplicate property found and will not be added again: ", currentProp.name)
                } else {
                    material.props.push(currentProp);
                    currentProp = {};
                }
            }
        } else {
            let parts = line.split('=');
            if (inProp) {
                if (parts[0] == "name") {
                    currentProp.name = parts[1];
                } else if (parts[0] == "type") {
                    currentProp.type = parts[1];
                } else if (parts[0] == "value") {
                    currentProp.value = parts[1];
                }
            } else if (inMap) {
                currentMap[parts[0]] = parts[1];
            } else {
                // Generic property
                if (parts[0] == "type") {
                    material.type = parts[1];
                } else if (parts[0] == "shader") {
                    if (parts[1].toLowerCase() != "shader.pbrmaterial") {
                        material.shader = parts[1];
                    }
                }
            }
        }
    }

    // TODO: create KSON file
    let outstr = "";
    outstr += `version = ${material.version}\n`;
    outstr += `type = ${material.type.toLowerCase()}\n\n`;
    outstr += `maps = [\n`;
    for (const map of material.maps) {
        outstr += `\t{\n`;
        outstr += `\t\tname = \"${map.name}\"\n`;
        outstr += `\t\ttexture_name = \"${map.texture_name}\"\n`;
        if (map.filter_min) {
            outstr += `\t\tfilter_min = \"${map.filter_min}\"\n`;
        }
        if (map.filter_mag) {
            outstr += `\t\tfilter_mag = \"${map.filter_mag}\"\n`;
        }
        if (map.repeat_u) {
            outstr += `\t\trepeat_u = \"${map.repeat_u}\"\n`;
        }
        if (map.repeat_v) {
            outstr += `\t\trepeat_v = \"${map.repeat_v}\"\n`;
        }
        if (map.repeat_w) {
            outstr += `\t\trepeat_w = \"${map.repeat_w}\"\n`;
        }
        outstr += `\t}\n`;
    }
    outstr += `]\n\n`;

    outstr += `properties = [\n`;
    for (let prop of material.props) {
        outstr += `\t{\n`;
        outstr += `\t\tname = \"${prop.name}\"\n`;
        outstr += `\t\ttype = \"${prop.type}\"\n`;
        switch (prop.type.toLowerCase()) {
            case "string":
            case "vec4":
            case "vec3":
            case "vec2":
            case "mat4":
                outstr += `\t\tvalue = \"${prop.value}\"\n`;
                break;
            default:
                outstr += `\t\tvalue = ${prop.value}\n`;
                break;
        }
        outstr += `\t}\n`;
    }
    outstr += `]\n`;
    // console.log(outstr);
    fs.renameSync(filename, filename + '.old');
    fs.writeFileSync(filename, outstr);
}

let dirContents = fs.readdirSync(".");
for (let file of dirContents) {
    if (file.endsWith('.kmt')) {
        console.log(file);
        upgrade_file(file);
    }
    if (deleteOldFiles && file.endsWith('.kmt.old')) {
        console.log("Deleting old file " + file + ".");
        fs.rmSync(file);
    }
}